#include "co_rwmutex.h"
#include "../scheduler/scheduler.h"

namespace co
{

CoRWMutex::CoRWMutex(bool writePriority)
{
    lockState_ = 0;
    writePriority_ = writePriority;
    readView_.self_ = writeView_.self_ = this;
}
CoRWMutex::~CoRWMutex()
{
    assert(lock_.try_lock());
    assert(lockState_ == 0);
    assert(rQueue_.empty());
    assert(wQueue_.empty());
    readView_.self_ = writeView_.self_ = nullptr;
}

void CoRWMutex::RLock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (writePriority_) {
        // 写优先
        if (lockState_ >= 0 && wQueue_.empty()) {
            ++lockState_;
            return ;
        }
    } else {
        // 读优先
        if (lockState_ >= 0) {
            ++lockState_;
            return ;
        }
    }

    if (Processer::IsCoroutine()) {
        // 协程
        rQueue_.push(Processer::Suspend());
        lock.unlock();
        Processer::StaticCoYield();
    } else {
        // 原生线程
        rQueue_.push(Processer::SuspendEntry{});
        rCv_.wait(lock);
    }
}
bool CoRWMutex::RTryLock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (lockState_ >= 0) {
        ++lockState_;
        return true;
    }
    return false;
}
void CoRWMutex::RUnlock()
{
    std::unique_lock<LFLock> lock(lock_);
    assert(lockState_ > 0);
    if (--lockState_ > 0)
        return ;

    TryWakeUp();
}

void CoRWMutex::WLock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (lockState_ == 0) {
        lockState_ = -1;
        return ;
    }

    if (Processer::IsCoroutine()) {
        // 协程
        wQueue_.push(Processer::Suspend());
        lock.unlock();
        Processer::StaticCoYield();
    } else {
        // 原生线程
        wQueue_.push(Processer::SuspendEntry{});
        wCv_.wait(lock);
    }
}
bool CoRWMutex::WTryLock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (lockState_ == 0) {
        lockState_ = -1;
        return true;
    }
    return false;
}
void CoRWMutex::WUnlock()
{
    std::unique_lock<LFLock> lock(lock_);
    assert(lockState_ == -1);

    TryWakeUp();
}

void CoRWMutex::TryWakeUp()
{
    lockState_ = -1;

    // 优先唤醒写等待
    while (!wQueue_.empty()) {
        auto entry = wQueue_.front();
        wQueue_.pop();

        if (entry) {
            // 协程
            if (Processer::Wakeup(entry))
                return ;
        } else {
            // 原生线程
            wCv_.notify_one();
            return ;
        }
    }

    lockState_ = 0;

    // 唤醒读等待
    while (!rQueue_.empty()) {
        auto entry = rQueue_.front();
        rQueue_.pop();

        if (entry) {
            // 协程
            if (Processer::Wakeup(entry))
                ++lockState_;
        } else {
            // 原生线程
            rCv_.notify_one();
            ++lockState_;
        }
    }
}

bool CoRWMutex::IsLock()
{
    std::unique_lock<LFLock> lock(lock_);
    return lockState_ == -1;
}

CoRWMutex::ReadView & CoRWMutex::Reader()
{
    return readView_;
}
CoRWMutex::WriteView & CoRWMutex::Writer()
{
    return writeView_;
}
CoRWMutex::ReadView & CoRWMutex::reader()
{
    return readView_;
}
CoRWMutex::WriteView & CoRWMutex::writer()
{
    return writeView_;
}

void CoRWMutex::ReadView::lock()
{
    self_->RLock();
}
bool CoRWMutex::ReadView::try_lock()
{
    return self_->RTryLock();
}
bool CoRWMutex::ReadView::is_lock()
{
    return self_->IsLock();
}
void CoRWMutex::ReadView::unlock()
{
    return self_->RUnlock();
}

void CoRWMutex::WriteView::lock()
{
    self_->WLock();
}
bool CoRWMutex::WriteView::try_lock()
{
    return self_->WTryLock();
}
bool CoRWMutex::WriteView::is_lock()
{
    return self_->IsLock();
}
void CoRWMutex::WriteView::unlock()
{
    self_->WUnlock();
}

} //namespace co
