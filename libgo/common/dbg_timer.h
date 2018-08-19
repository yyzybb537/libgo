#pragma once
#include "config.h"
#include "clock.h"

namespace co {

#define DBG_TIMER_CHECK(t) do {\
        (t).check(__LINE__, std::chrono::microseconds(1)); \
    } while (0)

struct DbgTimer {
    explicit DbgTimer(uint64_t dbgMask) {
        active_ = (CoroutineOptions::getInstance().debug & dbgMask) != 0;
        if (!active_) return ;

        costs_.reserve(32);
        prev_ = FastSteadyClock::now();
    }

    template <typename Duration>
    void check(int line, Duration precision) {
        if (!active_) return ;

        auto now = FastSteadyClock::now();
        if (now - prev_ > precision) {
            costs_.push_back(std::pair<int, int>(line, std::chrono::duration_cast<std::chrono::microseconds>(now - prev_).count()));
        }
        prev_ = now;
    }

    std::string ToString() {
        if (!active_) return "";

        DBG_TIMER_CHECK(*this);

        std::string s;
        s.reserve(costs_.size() * 32);
        char buf[32] = {};
        for (auto & kv : costs_) {
             int len = snprintf(buf, sizeof(buf) - 1, "(L=%d, %d us)", kv.first, kv.second);
             s.append(buf, len);
        }
        return s;
    }

private:
    bool active_;
    FastSteadyClock::time_point prev_;
    std::vector<std::pair<int, int>> costs_;
};

} // namespace co
