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
              "sizeof(SharedMutexInternal_Fast) must equal sizeof(uint64_t)");

struct SharedMutex : public DebuggerId<SharedMutex>
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
        SharedMutexInternal* atomicInternal = (SharedMutexInternal*)whole();

        int32_t expect = atomicInternal->locked.load(std::memory_order_relaxed);
        if (expect != 0 && expect != -2) {
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d} | is locked | fail",
                    id(), readers_.id(), writers_.id(), __func__, expect);
            return false;
        }

        bool succ = atomicInternal->locked.compare_exchange_strong(expect, -1,
                    std::memory_order_acquire, std::memory_order_relaxed);
        if (succ) {
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " old-internal {locked=%d} | success",
                    id(), readers_.id(), writers_.id(), __func__, expect);
        } else {
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d} | exchange fail",
                    id(), readers_.id(), writers_.id(), __func__, expect);
        }
        return succ;
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

        // 1.解锁
        do {
            v = whole()->load(std::memory_order_relaxed);
            if (-1 != internal->locked) {
                // 异常锁状态
                RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                        " internal {locked=%d,w=%d,r=%d} | fatal: not locked",
                        id(), readers_.id(), writers_.id(), __func__,
                        (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                return ;
            }

            expect = v;
            internal->locked = internal->write_wait ? -2 : 0;
            // @此处不抹除write_wait, 唤醒失败再抹除.
            //internal->write_wait = 0;
        } while (!whole()->compare_exchange_strong(expect, v,
                    std::memory_order_release, std::memory_order_acquire));

        // 2.唤醒其他等待的routine
        notify(v, internal, __func__);
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
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d,w=%d,r=%d} | is locked | fail",
                    id(), readers_.id(), writers_.id(), __func__,
                    (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
            return false;
        }

        uint64_t expect = v;
        internal->locked ++;
        bool succ = whole()->compare_exchange_strong(expect, v,
                    std::memory_order_acquire, std::memory_order_relaxed);
        if (succ) {
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d,w=%d,r=%d} | success",
                    id(), readers_.id(), writers_.id(), __func__,
                    (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
        } else {
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d,w=%d,r=%d} | lock exchange fail",
                    id(), readers_.id(), writers_.id(), __func__,
                    (int)((FastSharedMutexInternal*)&expect)->locked,
                    (int)((FastSharedMutexInternal*)&expect)->write_wait,
                    (int)((FastSharedMutexInternal*)&expect)->read_wait);
        }
        return succ;
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

        // 1.解锁
        do {
            v = whole()->load(std::memory_order_relaxed);
            if (internal->locked <= 0) {
                //  异常锁状态
                RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                        " internal {locked=%d,w=%d,r=%d} | fatal: locked value exception",
                        id(), readers_.id(), writers_.id(), __func__,
                        (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                return ;
            }

            expect = v;
            if (internal->locked == 1 && internal->write_wait)
                internal->locked = -2;
            else
                internal->locked --;
        } while (!whole()->compare_exchange_strong(expect, v,
                    std::memory_order_release, std::memory_order_acquire));

        if (internal->locked > 0) {
            // 还有其他读锁locked, 不需要唤醒
            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d,w=%d,r=%d} | neednot notify | done",
                    id(), readers_.id(), writers_.id(), __func__,
                    (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
            return ;
        }

        notify(v, internal, __func__);
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
        RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | enter",
                id(), readers_.id(), writers_.id(), __func__, !!abstime);

        for (;;) {
            uint64_t v = whole()->load(std::memory_order_relaxed);
            FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                    " internal {locked=%d,w=%d,r=%d}",
                    id(), readers_.id(), writers_.id(), __func__, !!abstime,
                    (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);

            if (0 == internal->locked || -2 == internal->locked) {
                uint64_t expect = v;
                internal->locked = -1;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                            " expect={locked=%d,w=%d,r=%d} | lock exchange fail",
                            id(), readers_.id(), writers_.id(), __func__, !!abstime,
                            (int)((FastSharedMutexInternal*)&expect)->locked,
                            (int)((FastSharedMutexInternal*)&expect)->write_wait,
                            (int)((FastSharedMutexInternal*)&expect)->read_wait);
                    continue;
                }

                RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                        " lock success",
                        id(), readers_.id(), writers_.id(), __func__, !!abstime);
                return true;
            }

            // 加锁失败, wait
            if (0 == internal->write_wait) {
                uint64_t expect = v;
                internal->write_wait = 1;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                            " expect={locked=%d,w=%d,r=%d} | write wait exchange fail",
                            id(), readers_.id(), writers_.id(), __func__, !!abstime,
                            (int)((FastSharedMutexInternal*)&expect)->locked,
                            (int)((FastSharedMutexInternal*)&expect)->write_wait,
                            (int)((FastSharedMutexInternal*)&expect)->read_wait);
                    continue;
                }
            }

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | begin wait_until",
                    id(), readers_.id(), writers_.id(), __func__, !!abstime);

            auto res = writers_.wait_until(v, abstime);
            if (res == RutexBase::rutex_wait_return_etimeout) {
                RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | wait_until return timeout",
                        id(), readers_.id(), writers_.id(), __func__, !!abstime);
                return false;
            }

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | wait_until return success",
                    id(), readers_.id(), writers_.id(), __func__, !!abstime);
        }
    }

    template<typename _Clock, typename _Duration>
    bool lock_shared(const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | enter",
                id(), readers_.id(), writers_.id(), __func__, !!abstime);

        for (;;) {
            uint64_t v = whole()->load(std::memory_order_relaxed);
            FastSharedMutexInternal* internal = (FastSharedMutexInternal*)&v;

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                    " internal {locked=%d,w=%d,r=%d}",
                    id(), readers_.id(), writers_.id(), __func__, !!abstime,
                    (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);

            if (!internal->write_wait) { // 写优先需要这一行判断
                if (internal->locked >= 0 && internal->locked < 0x7fffffff) {
                    uint64_t expect = v;
                    internal->locked ++;
                    if (!whole()->compare_exchange_strong(expect, v,
                                std::memory_order_release, std::memory_order_acquire))
                    {
                        RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                                " expect={locked=%d,w=%d,r=%d} | shared_lock exchange fail",
                                id(), readers_.id(), writers_.id(), __func__, !!abstime,
                                (int)((FastSharedMutexInternal*)&expect)->locked,
                                (int)((FastSharedMutexInternal*)&expect)->write_wait,
                                (int)((FastSharedMutexInternal*)&expect)->read_wait);
                        continue;
                    }

                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                            " shared_lock success",
                            id(), readers_.id(), writers_.id(), __func__, !!abstime);
                    return true;
                }
            }

            if (0 == internal->read_wait) {
                uint64_t expect = v;
                internal->read_wait = 1;
                if (!whole()->compare_exchange_strong(expect, v,
                            std::memory_order_release, std::memory_order_acquire))
                {
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d |"
                            " expect={locked=%d,w=%d,r=%d} | read wait exchange fail",
                            id(), readers_.id(), writers_.id(), __func__, !!abstime,
                            (int)((FastSharedMutexInternal*)&expect)->locked,
                            (int)((FastSharedMutexInternal*)&expect)->write_wait,
                            (int)((FastSharedMutexInternal*)&expect)->read_wait);
                    continue;
                }
            }

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | begin wait_until",
                    id(), readers_.id(), writers_.id(), __func__, !!abstime);

            auto res = readers_.wait_until(v, abstime);
            if (res == RutexBase::rutex_wait_return_etimeout) {
                RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | wait_until return timeout",
                        id(), readers_.id(), writers_.id(), __func__, !!abstime);
                return false;
            }

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s | abstime=%d | wait_until return success",
                    id(), readers_.id(), writers_.id(), __func__, !!abstime);
        }
    }

    void notify(uint64_t & v, FastSharedMutexInternal* internal, const char* funcName)
    {
        // 执行notify函数之前处于解锁状态, 可能会有其他线程并发执行lock/unlock等函数,
        // 因此要求notify函数可并发执行.
        //
        // 幸运的是, 处于等待状态的writer和reader都可以被无效唤醒, 而不会导致逻辑出错.
        //
        // writer只能逐个唤醒, 所以writer使用如下流程:
        //   判断wait -> 唤醒 -> 唤醒失败删除wait(校验locked状态)
        //
        // reader要全量唤醒, 所以可以使用更简单的流程:
        //   删除wait -> notify_all

        // 1.尝试唤醒writer (写优先)
        if (internal->write_wait) {
            assert(internal->locked == -2 && "notify write, but locked isnot -2.");

            if (writers_.notify_one()) {
                // 唤醒成功
                RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                        " internal {locked=%d,w=%d,r=%d} | notify writer success | done",
                        id(), readers_.id(), writers_.id(), funcName,
                        (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                return ;
            }

            // 唤醒失败, 说明没有writer在等待, 设置write_wait = 0, 然后尝试唤醒reader
            uint64_t expect;
            do {
                v = whole()->load(std::memory_order_relaxed);
                if (internal->locked != -2) {
                    // 此时有并发, 因为上文设置过locked == -2, 所以其他线程并发的第一个接口必然是lock/try_lock.
                    // 随后也必然有对应的unlock, 并且触发notify, 当前流程可以终止了.
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                            " internal {locked=%d,w=%d,r=%d} | stop",
                            id(), readers_.id(), writers_.id(), funcName,
                            (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                    return ;
                }

                if (!internal->write_wait) {
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                            " internal {locked=%d,w=%d,r=%d} | other thread reset write_wait",
                            id(), readers_.id(), writers_.id(), funcName,
                            (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                    break;
                }

                // 即使locked == -2, 此处依然可能有并发 (其他线程lock后, 处于unlock后的notify流程中)
                // 此处设置的locked = 0, 可能导致其他线程唤醒的writer lock失败(被其他reader抢占),
                // 重走竞争流程, 但这无伤大雅.

                expect = v;
                internal->locked = 0;
                internal->write_wait = 0;
            } while (!whole()->compare_exchange_strong(expect, v,
                        std::memory_order_release, std::memory_order_acquire));
        }

        // 2.尝试唤醒reader
        if (internal->read_wait && internal->locked >= 0 && internal->write_wait == 0) {
            uint64_t expect;
            do {
                v = whole()->load(std::memory_order_relaxed);
                if (internal->locked < 0) {
                    // 写锁定, 无需再唤醒reader
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                            " internal {locked=%d,w=%d,r=%d} | other thread locked | stop notify reader",
                            id(), readers_.id(), writers_.id(), funcName,
                            (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                    return;
                }
                
                if (internal->write_wait) { // 写优先
                    // 有writer在等待了, 无需唤醒reader了
                    RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                            " internal {locked=%d,w=%d,r=%d} | other thread writer waiting | stop notify reader",
                            id(), readers_.id(), writers_.id(), funcName,
                            (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
                    return ;
                }

                expect = v;
                internal->read_wait = 0;
            } while (!whole()->compare_exchange_strong(expect, v,
                        std::memory_order_release, std::memory_order_acquire));

            int nr = readers_.notify_all();
            (void)nr;

            RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                    " internal {locked=%d,w=%d,r=%d} | notify all readers(%d)",
                    id(), readers_.id(), writers_.id(), funcName,
                    (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait,
                    nr);
            return ;
        }

        RS_DBG(dbg_mutex, "shared_mutex=%ld(r=%ld w=%ld) | %s |"
                " internal {locked=%d,w=%d,r=%d} | end | done",
                id(), readers_.id(), writers_.id(), funcName,
                (int)internal->locked, (int)internal->write_wait, (int)internal->read_wait);
    }

private:
    inline std::atomic<uint64_t>* whole() { return readers_.value(); }

private:
    Rutex<uint64_t, true> readers_;
    Rutex<uint64_t> writers_;
};

} // namespace libgo
