#pragma once
#include "mutex.h"
#include <condition_variable>
#include <mutex>

namespace libgo
{

struct ConditionVariable : public DebuggerId<ConditionVariable>
{
public:
    typedef Rutex<unsigned> RutexType;

    ConditionVariable() noexcept {}
    ~ConditionVariable() noexcept {}

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    // @return: 返回唤醒的routine数量下限, 实际唤醒数量可能大于返回值
    int notify_one()
    {
        int32_t val = rutex_.value()->fetch_add(1, std::memory_order_release);
        (void)val;
        int res = rutex_.notify_one();
        RS_DBG(dbg_cond_v, "cond=%ld rutex=%ld | %s | val=%d | return %d",
            id(), rutex_.id(), __func__, val + 1, res);
        return res;
    }

    // @return: 返回唤醒的routine数量下限, 实际唤醒数量可能大于返回值
    int notify_all()
    {
        int32_t val = rutex_.value()->fetch_add(1, std::memory_order_release);
        (void)val;
        int res = rutex_.notify_all();
        RS_DBG(dbg_cond_v, "cond=%ld rutex=%ld | %s | val=%d | return %d",
            id(), rutex_.id(), __func__, val + 1, res);
        return res;
    }

    // 快速notify_all, 要求ConditionVariable的每个wait和notify都只搭配同一把锁使用
    // @lock: 必须在lock锁定状态才能调用这个函数.
    // @return: 返回唤醒的routine数量下限, 实际唤醒数量可能大于返回值
    int fast_notify_all(std::unique_lock<Mutex>& lock)
    {
        int32_t val = rutex_.value()->fetch_add(1, std::memory_order_release);
        (void)val;
        int res = lock.mutex()->requeue_in_lock(&rutex_);
        RS_DBG(dbg_cond_v, "cond=%ld rutex=%ld | %s | val=%d | return %d",
            id(), rutex_.id(), __func__, val + 1, res);
        return res;
    }

    void wait(std::unique_lock<Mutex>& lock)
    {
        wait_until_impl(lock, RoutineSyncTimer::null_tp());
    }

    template<typename Predicate>
    void wait(std::unique_lock<Mutex>& lock, Predicate p)
    {
        while (!p())
            wait(lock);
    }

    template<typename _Clock, typename _Duration>
    std::cv_status wait_until(std::unique_lock<Mutex>& lock,
            const std::chrono::time_point<_Clock, _Duration>& abstime)
    {
        return wait_until_impl(lock, &abstime);
    }

    template<typename _Clock, typename _Duration, typename Predicate>
    bool wait_until(std::unique_lock<Mutex>& lock,
        const std::chrono::time_point<_Clock, _Duration>& abstime,
        Predicate p)
    {
        while (!p())
            if (wait_until(lock, abstime) == std::cv_status::timeout)
                return p();

        return true;
    }

    template<typename _Rep, typename _Period>
    std::cv_status wait_for(std::unique_lock<Mutex>& lock,
        const std::chrono::duration<_Rep, _Period>& dur)
    {
        return wait_until(lock, RoutineSyncTimer::now() + dur);
    }

    template<typename _Rep, typename _Period, typename Predicate>
    bool wait_for(std::unique_lock<Mutex>& lock,
            const std::chrono::duration<_Rep, _Period>& dur,
            Predicate p)
    {
        return wait_until(lock, RoutineSyncTimer::now() + dur, std::move(p));
    }

    template<typename _Clock, typename _Duration, typename Predicate>
    std::cv_status wait_until_p(std::unique_lock<Mutex>& lock,
            const std::chrono::time_point<_Clock, _Duration> * abstime,
            Predicate p)
    {
        if (abstime) {
            return wait_until(lock, *abstime, p) ? std::cv_status::no_timeout
                : std::cv_status::timeout;
        }

        wait(lock, p);
        return std::cv_status::no_timeout;
    }

public:
    template<typename _Clock, typename _Duration>
    std::cv_status wait_until_impl(std::unique_lock<Mutex>& lock,
            const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        const int32_t expectedValue = rutex_.value()->load(std::memory_order_relaxed);
        lock.mutex()->unlock();

        RS_DBG(dbg_cond_v, "cond=%ld rutex=%ld | %s | expectedValue=%d | abstime=%d | begin wait_until",
            id(), rutex_.id(), __func__, expectedValue, !!abstime);

        RutexBase::rutex_wait_return res = rutex_.wait_until(expectedValue, abstime);

        std::cv_status status = (
                RutexBase::rutex_wait_return_etimeout == res)
            ? std::cv_status::timeout : std::cv_status::no_timeout;

        RS_DBG(dbg_cond_v, "cond=%ld rutex=%ld | %s | expectedValue=%d | wait_until return %s | status=%s",
            id(), rutex_.id(), __func__, expectedValue, RutexBase::etos(res),
            status == std::cv_status::no_timeout ? "no_timeout" : "timeout");

        lock.mutex()->lock_contended(RoutineSyncTimer::null_tp());
        return status;
    }

private:
    RutexType rutex_;
};

} // namespace libgo
