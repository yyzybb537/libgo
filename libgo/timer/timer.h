#pragma once
#include "../common/config.h"
#include "../common/spinlock.h"
#include "../common/timer.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"
#include "../sync/channel.h"

namespace co
{

class CoTimer {
    class CoTimerImpl : private Timer<std::function<void()>>
    {
    public:
        typedef std::function<void()> func_t;
        typedef Timer<func_t> base_t;
        typedef base_t::TimerId TimerId;

        explicit CoTimerImpl(FastSteadyClock::duration precision);
        ~CoTimerImpl();

        void BindScheduler(Scheduler* scheduler);

        TimerId ExpireAt(FastSteadyClock::duration dur, func_t const& cb);

        void RunInCoroutine();

        void Stop();

    private:
        LFLock lock_;

        // 精度
        FastSteadyClock::duration precision_;

        Channel<void> trigger_{1};

        Channel<void> quit_{1};

        volatile bool terminate_ = false;

        Scheduler* scheduler_ = nullptr;
    };

public:
    typedef CoTimerImpl::func_t func_t;
    typedef CoTimerImpl::TimerId TimerId;

public:
    template <typename Rep, typename Period>
    explicit CoTimer(std::chrono::duration<Rep, Period> dur, Scheduler * scheduler = nullptr)
        : impl_(new CoTimerImpl(std::chrono::duration_cast<FastSteadyClock::duration>(dur)))
    {
        Initialize(scheduler);
    }

    explicit CoTimer(Scheduler * scheduler = nullptr)
        : CoTimer(std::chrono::milliseconds(1), scheduler)
    {}

    ~CoTimer();

    TimerId ExpireAt(FastSteadyClock::duration dur, func_t const& cb);

    TimerId ExpireAt(FastSteadyClock::time_point tp, func_t const& cb);

    template <typename Rep, typename Period>
    TimerId ExpireAt(std::chrono::duration<Rep, Period> dur, func_t const& fn) {
        return ExpireAt(std::chrono::duration_cast<FastSteadyClock::duration>(dur), fn);
    }

private:
    CoTimer(CoTimer const&) = delete;
    CoTimer& operator=(CoTimer const&) = delete;

    void Initialize(Scheduler * scheduler);

private:
    std::shared_ptr<CoTimerImpl> impl_;
};

} //namespace co
