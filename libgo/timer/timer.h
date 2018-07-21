#pragma once
#include "../common/config.h"
#include "../common/spinlock.h"
#include "../common/timer.h"
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

        CoTimerImpl(bool highResolution = false);
        ~CoTimerImpl();

        TimerId ExpireAt(FastSteadyClock::duration dur, func_t const& cb);

        void RunInCoroutine();

        void Stop();

    private:
        LFLock lock_;

        bool highResolution_;

        Channel<void> trigger_{1};

        volatile bool terminate_ = false;
    };

public:
    typedef CoTimerImpl::func_t func_t;
    typedef CoTimerImpl::TimerId TimerId;

public:
    explicit CoTimer(bool highResolution = false);
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

private:
    std::shared_ptr<CoTimerImpl> impl_;
};

} //namespace co
