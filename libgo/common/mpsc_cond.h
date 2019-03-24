#pragma once
#include "config.h"
#include <condition_variable>
#include <thread>

namespace co {

class mpsc_spin_condition_variable
{
    enum {
        s_idle,
        s_notifing,
        s_wait,
    };

    atomic_t<int> state_{s_idle};
    std::mutex mtx_;
    std::condition_variable cond_;
    int spin_ = 64;

public:
    bool is_waiting() {
        return s_wait == state_.load(std::memory_order_relaxed);
    }

    void wait()
    {
        for (int i = 0; i < spin_; ++i) {
            int state = state_.load(std::memory_order_relaxed);
            if (state == s_notifing) {
                state_.store(s_idle, std::memory_order_relaxed);
                return ;
            }

            std::this_thread::yield();
        }

        std::unique_lock<std::mutex> lock(mtx_);
        for (;;) {
            int state = state_.load(std::memory_order_relaxed);
            if (state == s_notifing)
                return ;

            if (state_.compare_exchange_weak(state, s_wait,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                break;
        }

        cond_.wait(lock);
    }

    void notify()
    {
        for (;;) {
            int state = state_.load(std::memory_order_relaxed);
            if (state == s_notifing)
                return ;

            if (state == s_wait) {
                std::unique_lock<std::mutex> lock(mtx_);
                state = state_.load(std::memory_order_relaxed);
                if (state == s_notifing)
                    return ;

				if (state == s_wait) {
					cond_.notify_one();
					state_.store(s_idle, std::memory_order_relaxed);
					return ;
				}
			}

			if (state_.compare_exchange_weak(state, s_notifing,
						std::memory_order_acq_rel, std::memory_order_relaxed))
				break;
		}
    }
};

} //namespace co
