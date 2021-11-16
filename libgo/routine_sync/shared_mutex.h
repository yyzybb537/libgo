#pragma once
#include "rutex.h"
#include "condition_variable.h"

namespace libgo
{

// Implement bthread_mutex_t related functions
struct SharedMutexInternal {
    // -2: waiting for write lock; 避免unlock后被reader强占locked 
    // -1: write locked
    // 0: not lock
    // 1: read locked
    std::atomic<int32_t> locked;
    std::atomic<uint8_t> write_wait;
    std::atomic<uint8_t> read_wait;
    uint16_t padding;
};

struct FastSharedMutexInternal {
    int32_t locked;
    uint8_t write_wait;
    uint8_t read_wait;
    uint16_t padding;
};

static_assert(sizeof(uint64_t) == sizeof(SharedMutexInternal),
              "sizeof(SharedMutexInternal) must equal sizeof(uint64_t)");

static_assert(sizeof(uint64_t) == sizeof(FastSharedMutexInternal),
              "sizeof(SharedMutexInternal_Fast) must equal sizeof(unsigned)");

struct SharedMutex
{
public:
    SharedMutex() noexcept {
        readers_.ref(writers_.value());
    }

    ~SharedMutex() noexcept {}

    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    // ---------------- write lock
    void lock()
    {
        lock(RoutineSyncTimer::null_tp());
    }

    bool try_lock()
    {
        SharedMutexInternal* internal = (SharedMutexInternal*)whole();
        int32_t expect = 0;
        return internal->locked.compare_exchange_strong(expect, -1,
                    std::memory_order_acquire, std::memory_order_relaxed);
    }

    // todo: shared_mutex的is_lock怎么定义？
    bool is_lock()
    {
        SharedMutexInternal* internal = (SharedMutexInternal*)whole();
        return internal->locked.load(std::memory_order_relaxed) == -1;
    }

    void unlock()
    {
        uint64_t v;
        uint64_t expect;
        FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;
        int32_t locked = 0;

        // 1.解锁, 抹去write_wait
        do {
            v = whole()->load(std::memory_order_relaxed);
            if (-1 != internal->locked)
                return ;

            locked = internal->write_wait ? -2 : 0;

            expect = v;
            internal->locked = locked;
            internal->write_wait = 0;
        } while (!whole()->compare_exchange_strong(expect, v,
                    std::memory_order_release, std::memory_order_acquire));

        // 2.尝试唤醒writer (写优先)
        if (locked == -2) {
            if (writers_.notify_one())
                return ;
        }

        // 3.尝试唤醒writer or reader or locked还原成0
        for (;;) {
            v = whole()->load(std::memory_order_relaxed);
            if (locked == -2 && locked != internal->locked)
                return ;    // 另一个writer获取了锁, 无须再唤醒了

            if (internal->write_wait) {
                expect = v;
                internal->write_wait = 0;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    continue;
                }

                if (writers_.notify_one()) {
                    return ;
                }
            }

            if (internal->read_wait) {
                expect = v;
                internal->locked = 0;
                internal->read_wait = 0;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    continue;
                }

                readers_.notify_all();
                return ;
            }

            if (locked == -2) {
                expect = v;
                internal->locked = 0;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    continue;
                }
            }

            break;
        }
    }

    template<typename _Clock, typename _Duration>
    bool try_lock_until(const std::chrono::time_point<_Clock, _Duration> & abstime)
    {
        return lock(&abstime);
    }

    template <class Rep, class Period>
    bool try_lock_for( const std::chrono::duration<Rep,Period>& timeout_duration )
    {
        auto tp = RoutineSyncTimer::now() + timeout_duration;
        return lock(&tp);
    }

    // ---------------- read lock
    void lock_shared()
    {
        lock_shared(RoutineSyncTimer::null_tp());
    }

    bool try_lock_shared()
    {
        uint64_t v = whole()->load(std::memory_order_relaxed);
        FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;
        if (internal->locked < 0) {
            return false;
        }

        if (internal->write_wait) // 写优先需要这一行判断
            return false;

        uint64_t expect = v;
        internal->locked ++;
        return whole()->compare_exchange_strong(expect, v,
                    std::memory_order_acquire, std::memory_order_relaxed);
    }

    bool is_lock_shared()
    {
        SharedMutexInternal* internal = (SharedMutexInternal*)whole();
        return internal->locked.load(std::memory_order_relaxed) > 0;
    }

    void unlock_shared()
    {
        uint64_t v;
        uint64_t expect;
        FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;
        bool bWakeWriter = false;

        // 1.解锁, 抹去write_wait
        do {
            v = whole()->load(std::memory_order_relaxed);
            if (internal->locked <= 0)
                return ;

            bWakeWriter = internal->locked == 1 && internal->write_wait;

            expect = v;
            internal->locked --;
            if (bWakeWriter) {
                internal->write_wait = 0;
            }
        } while (!whole()->compare_exchange_strong(expect, v,
                    std::memory_order_release, std::memory_order_acquire));

        // 2.尝试唤醒writer
        if (bWakeWriter) {
            if (writers_.notify_one())
                return ;
        }
    }

    template<typename _Clock, typename _Duration>
    bool try_lock_shared_until(const std::chrono::time_point<_Clock, _Duration> & abstime)
    {
        return lock_shared(&abstime);
    }

    template <class Rep, class Period>
    bool try_lock_shared_for( const std::chrono::duration<Rep,Period>& timeout_duration )
    {
        auto tp = RoutineSyncTimer::now() + timeout_duration;
        return lock_shared(&tp);
    }

    bool is_idle()
    {
        SharedMutexInternal* internal = (SharedMutexInternal*)whole();
        return internal->locked.load(std::memory_order_relaxed) == 0;
    }

public:
    template<typename _Clock, typename _Duration>
    bool lock(const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        for (;;) {
            uint64_t v = whole()->load(std::memory_order_relaxed);
            FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;
            if (0 == internal->locked || -2 == internal->locked) {
                uint64_t expect = v;
                internal->locked = -1;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    continue;
                }

                return true;
            }

            if (0 == internal->write_wait) {
                uint64_t expect = v;
                internal->write_wait = 1;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    continue;
                }
            }

            auto res = writers_.wait_until(v, abstime);
            if (res == RutexBase::rutex_wait_return_etimeout)
                return false;
        }
    }

    template<typename _Clock, typename _Duration>
    bool lock_shared(const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        for (;;) {
            uint64_t v = whole()->load(std::memory_order_relaxed);
            FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;

            if (!internal->write_wait) { // 写优先需要这一行判断
                if (internal->locked >= 0 && internal->locked < 0x7fffffff) {
                    uint64_t expect = v;
                    internal->locked ++;
                    if (!whole()->compare_exchange_strong(expect, v,
                                std::memory_order_release, std::memory_order_acquire))
                    {
                        continue;
                    }

                    return true;
                }
            }

            if (0 == internal->read_wait) {
                uint64_t expect = v;
                internal->read_wait = 1;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    continue;
                }
            }

            auto res = readers_.wait_until(v, abstime);
            if (res == RutexBase::rutex_wait_return_etimeout)
                return false;
        }
    }

private:
    inline std::atomic<uint64_t>* whole() { return readers_.value(); }

private:
    Rutex<uint64_t, true> readers_;
    Rutex<uint64_t> writers_;
};

} // namespace libgo
