#pragma once
#include <time.h>
#include <chrono>
#include <thread>
#include <limits>
#include "spinlock.h"

namespace co
{

class FastSteadyClock
{
public:
    typedef std::chrono::steady_clock base_clock_t;

    typedef base_clock_t::duration duration;
    typedef base_clock_t::rep rep;
    typedef base_clock_t::period period;
    typedef base_clock_t::time_point time_point;

    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        if (!self().fast_)
            return base_clock_t::now();
        else {
            auto &checkPoint = self().checkPoint_[self().switchIdx_];
            uint64_t dtsc = rdtsc() - checkPoint.tsc_;
            long dur = checkPoint.tp_.time_since_epoch().count() + dtsc / self().cycle_;
            return time_point(duration(dur));
        }
    }

    static void ThreadRun() {
        std::unique_lock<LFLock> lock(self().threadInit_, std::defer_lock);
        if (!lock.try_lock()) return;

        for (;;std::this_thread::sleep_for(std::chrono::milliseconds(20))) {
            auto &checkPoint = self().checkPoint_[!self().switchIdx_];
            checkPoint.tp_ = base_clock_t::now();
            checkPoint.tsc_ = rdtsc();

            auto &lastCheckPoint = self().checkPoint_[self().switchIdx_];
            if (lastCheckPoint.tsc_ != 0) {
                duration dur = checkPoint.tp_ - lastCheckPoint.tp_;
                uint64_t dtsc = checkPoint.tsc_ - lastCheckPoint.tsc_;
                float cycle = (float)dtsc / (std::max<long>)(dur.count(), 1);
                if (cycle < std::numeric_limits<float>::min())
                    cycle = std::numeric_limits<float>::min();
                self().cycle_ = cycle;
                self().fast_ = true;
            }
            
            self().switchIdx_ = !self().switchIdx_;
        }
    }

private:
    struct Data {
        struct CheckPoint {
            time_point tp_;
            uint64_t tsc_ = 0;
        };

        LFLock threadInit_;
        bool fast_ = false;
        double cycle_ = 1;
        CheckPoint checkPoint_[2];
        volatile int switchIdx_ = 0;
    };
    static Data& self() {
        static Data obj;
        return obj;
    }
    inline static uint64_t rdtsc() {
        uint32_t high, low;
        __asm__ __volatile__(
                "rdtsc" : "=a" (low), "=d" (high)
                );
        return ((uint64_t)high << 32) | low;
    }
};

} // namespace co
