#include "timer.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"
#include "../libgo.h"

namespace co
{

CoTimer::CoTimerImpl::CoTimerImpl(FastSteadyClock::duration precision)
    : precision_(precision)
{
//    trigger_.SetDbgMask(0);
    // printf("CoTimerImpl::CoTimerImpl threadid=%d\n", (int)syscall(SYS_gettid));
}

CoTimer::CoTimerImpl::~CoTimerImpl()
{

}

void CoTimer::CoTimerImpl::BindScheduler(Scheduler* scheduler)
{
    scheduler_ = scheduler;
}

void CoTimer::CoTimerImpl::RunInCoroutine()
{
    while (!terminate_) {
        DebugPrint(dbg_timer, "trigger RunOnce");
        RunOnce();

        if (terminate_) break;

        BindScheduler(Processer::GetCurrentScheduler());

        std::unique_lock<LFLock> lock(lock_);

        auto nextTime = NextTrigger(precision_);
        auto now = FastSteadyClock::now();
        auto nextDuration = nextTime > now ? nextTime - now : FastSteadyClock::duration(0);
        DebugPrint(dbg_timer, "wait trigger nextDuration=%d ns", (int)std::chrono::duration_cast<std::chrono::nanoseconds>(nextDuration).count());
        if (nextDuration.count() > 0) {
            trigger_.TimedPop(nullptr, nextDuration);
        } else {
			if (!trigger_.TryPop(nullptr))
				co_yield;
        }
    }

    DebugPrint(dbg_timer, "CoTimerImpl::RunInCoroutine will exit");
    quit_ << nullptr;
}

void CoTimer::CoTimerImpl::Stop()
{
    // printf("CoTimerImpl::Stop threadid=%d terminate_=%d isExiting=%d bindSched=%d isStop=%d\n", 
    //     (int)syscall(SYS_gettid), terminate_, Scheduler::IsExiting(),
    //     !!scheduler_, scheduler_ ? scheduler_->IsStop() : 0);

    if (terminate_)
        return ; 
    
    if (Scheduler::IsExiting())
        return ;
        
    if (scheduler_ && !scheduler_->IsStop()) {
        terminate_ = true;
        quit_ >> nullptr;
    }
}

CoTimer::CoTimerImpl::TimerId
CoTimer::CoTimerImpl::ExpireAt(FastSteadyClock::duration dur, func_t const& cb)
{
    DebugPrint(dbg_timer, "add timer dur=%d", (int)std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());

    auto id = StartTimer(dur, cb);

    // 强制唤醒, 提高精准度
    if (dur <= precision_) {
        std::unique_lock<LFLock> lock(lock_, std::defer_lock);
        if (lock.try_lock()) return id;

        trigger_.TryPush(nullptr);
    }

    return id;
}

void CoTimer::Initialize(Scheduler * scheduler)
{
    impl_->BindScheduler(scheduler);
    auto ptr = impl_;
    go co_scheduler(scheduler) [ptr] {
        ptr->RunInCoroutine();
    };
}

CoTimer::~CoTimer()
{
    impl_->Stop();
}

CoTimer::TimerId CoTimer::ExpireAt(FastSteadyClock::duration dur, func_t const& cb)
{
    return impl_->ExpireAt(dur, cb);
}

CoTimer::TimerId CoTimer::ExpireAt(FastSteadyClock::time_point tp, func_t const& cb)
{
    auto now = FastSteadyClock::now();
    auto dur = (tp > now) ? tp - now : FastSteadyClock::duration(0);
    return ExpireAt(dur, cb);
}

} //namespace co
