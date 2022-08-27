#include "timer.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"
#include "../libgo.h"

namespace co
{

void CoTimer::Initialize(Scheduler * scheduler)
{
    std::shared_ptr<CoroutineTimer> ptr = impl_;
    go co_scheduler(scheduler) [ptr] {
        ptr->run();
    };
}

CoTimer::~CoTimer()
{
    impl_->stop();
}

CoTimer::TimerId CoTimer::ExpireAt(FastSteadyClock::duration dur, func_t const& cb)
{
    auto tp = FastSteadyClock::now();
    tp += dur;
    return ExpireAt(tp, cb);
}

CoTimer::TimerId CoTimer::ExpireAt(FastSteadyClock::time_point tp, func_t const& cb)
{
    TimerIdImplPtr idImpl = std::make_shared<TimerIdImpl>(impl_);
    func_t f = [idImpl, cb] {
        cb();
    };
    impl_->schedule(idImpl->id_, tp, f);
    return TimerId(idImpl);
}

} //namespace co
