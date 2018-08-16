#include "async_coroutine_pool.h"
#include "../coroutine.h"

namespace co {

AsyncCoroutinePool * AsyncCoroutinePool::Create(size_t maxCallbackPoints)
{
    return new AsyncCoroutinePool(maxCallbackPoints);
}
void AsyncCoroutinePool::InitCoroutinePool(size_t maxCoroutineCount)
{
    maxCoroutineCount_ = maxCoroutineCount;
}
void AsyncCoroutinePool::Start(int minThreadNumber, int maxThreadNumber)
{
    if (!started_.try_lock()) return ;
    std::thread([=]{ 
                scheduler_->Start(minThreadNumber, maxThreadNumber); 
            }).detach();
    if (maxCoroutineCount_ == 0) {
        maxCoroutineCount_ = (std::max)(minThreadNumber * 128, maxThreadNumber);
        maxCoroutineCount_ = (std::min<size_t>)(maxCoroutineCount_, 10240);
    }
    for (size_t i = 0; i < maxCoroutineCount_; ++i) {
        go co_scheduler(scheduler_) [this]{
            this->Go();
        };
    }
}
void AsyncCoroutinePool::Go()
{
    for (;;) {
        PoolTask task;
        tasks_ >> task;

        if (task.func_)
            task.func_();

        if (!task.cb_)
            continue;

        size_t pointsCount = pointsCount_;
        if (!pointsCount) {
            task.cb_();
            continue;
        }

        size_t idx = ++robin_ % pointsCount;
        points_[idx]->Post(std::move(task.cb_));
        points_[idx]->Notify();
    }
}
void AsyncCoroutinePool::Post(Func const& func, Func const& callback)
{
    PoolTask task{func, callback};
    tasks_ << std::move(task);
}
bool AsyncCoroutinePool::AddCallbackPoint(AsyncCoroutinePool::CallbackPoint * point)
{
    size_t writeIdx = writePointsCount_++;
    if (writeIdx >= maxCallbackPoints_) {
        --writePointsCount_;
        return false;
    }

    points_[writeIdx] = point;
    while (!pointsCount_.compare_exchange_weak(writeIdx, writeIdx + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) ;
    return true;
}
AsyncCoroutinePool::AsyncCoroutinePool(size_t maxCallbackPoints)
{
    maxCoroutineCount_ = 0;
    maxCallbackPoints_ = maxCallbackPoints;
    scheduler_ = Scheduler::Create();
    points_ = new CallbackPoint*[maxCallbackPoints_];
}

size_t AsyncCoroutinePool::CallbackPoint::Run(size_t maxTrigger)
{
    size_t i = 0;
    for (; i < maxTrigger || maxTrigger == 0; ++i) {
        Func cb;
        if (!channel_.TryPop(cb))
            break;
        cb();
    }
    return i;
}

void AsyncCoroutinePool::CallbackPoint::Post(Func && cb)
{
    channel_ << std::move(cb);
}
void AsyncCoroutinePool::CallbackPoint::SetNotifyFunc(Func const& notify)
{
    notify_ = notify;
}
void AsyncCoroutinePool::CallbackPoint::Notify()
{
    if (notify_) notify_();
}

} // namespace co
