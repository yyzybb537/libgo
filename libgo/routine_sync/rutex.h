#pragma once
#include "switcher.h"
#include <atomic>
#include <mutex>
#include <memory>
#include "timer.h"

// 类似posix的futex
// 阻塞等待一个目标值

namespace libgo
{

template <typename IntValueType, bool Reference>
struct IntValue;

template <typename IntValueType>
struct IntValue<IntValueType, true>
{
public:
    inline std::atomic<IntValueType>* value() { return ptr_; }
    inline void ref(std::atomic<IntValueType>* ptr) { ptr_ = ptr; }

protected:
    std::atomic<IntValueType>* ptr_ {nullptr};
};

template <typename IntValueType>
struct IntValue<IntValueType, false>
{
public:
    inline std::atomic<IntValueType>* value() { return &value_; }

protected:
    std::atomic<IntValueType> value_ {0};
};

struct RutexBase
{
public:
    enum rutex_wait_return {
        rutex_wait_return_success = 0,
        rutex_wait_return_etimeout = 1,
        rutex_wait_return_ewouldblock = 2,
        rutex_wait_return_eintr = 3,
    };

    inline static const char* etos(rutex_wait_return v) {
#define __SWITCH_CASE_ETOS(x) case x: return #x
        switch (v) {
            __SWITCH_CASE_ETOS(rutex_wait_return_success);
            __SWITCH_CASE_ETOS(rutex_wait_return_etimeout);
            __SWITCH_CASE_ETOS(rutex_wait_return_ewouldblock);
            __SWITCH_CASE_ETOS(rutex_wait_return_eintr);
            default: return "Unknown rutex_wait_return";
        }
#undef __SWITCH_CASE_ETOS
    }

protected:
    friend struct RutexWaiter;
    LinkedList waiters_;
    std::mutex mtx_;
};

struct RutexWaiter : public LinkedNode, public DebuggerId<RutexWaiter>
{
public:
    enum waiter_state {
        waiter_state_none,
        waiter_state_ready,
        waiter_state_interrupted,
        waiter_state_timeout,
    };

    explicit RutexWaiter(RoutineSwitcherI & sw) : switcher_(&sw) {}

    void mark()
    {
        switcher_->mark();
    }

    template<typename _Clock, typename _Duration>
    void sleep(const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        if (abstime) {
            hasAbsTime_ = true;
            RoutineSyncTimer::getInstance().schedule(timerId_, *abstime,
                    [this] { this->wake_by_timer(); });
        }

        switcher_->sleep();
    }

    // 需要先加锁再执行
    bool wake(int state = waiter_state_ready) {
        if (waked_.load(std::memory_order_relaxed))
            return false;

        if (!switcher_->wake()) {
            return false;
        }

        state_ = state;
        waked_.store(true, std::memory_order_relaxed);
        return true;
    }

    void wake_by_timer() {
        std::unique_lock<std::mutex> lock(wakeMtx_, std::defer_lock);
        if (!lock.try_lock()) {
            RS_DBG(dbg_rutex, "rw=%ld | %s | try_lock=0",
                    id(), __func__);
            return ;
        }
        
        if (!safe_unlink()) {
            RS_DBG(dbg_rutex, "rw=%ld | %s | safe_unlink failed",
                    id(), __func__);
            return ;
        }

        if (wake(waiter_state_timeout)) {
            RS_DBG(dbg_rutex, "rw=%ld | %s | wake success",
                    id(), __func__);
            return ;
        }

        if (!waked_.load(std::memory_order_relaxed)) {
            RS_DBG(dbg_rutex, "rw=%ld | %s | wake failed, retry delay=%d",
                    id(), __func__, (int)delayMs_);
            // 可能还没来得及sleep, 往后等一等
            RoutineSyncTimer::getInstance().reschedule(timerId_,
                    RoutineSyncTimer::now() + std::chrono::milliseconds(delayMs_));
            delayMs_ = delayMs_ << 1;
        } else {
            RS_DBG(dbg_rutex, "rw=%ld | %s | wake failed, needn't retry.",
                    id(), __func__);
        }
    }

    inline bool safe_unlink();

    void join() {
        // ------- 1.已唤醒, 避免不必要的wake调用, 加速wake结束
        waked_.store(true, std::memory_order_relaxed);

        // ------- 2.先unlink
        // 有3种结果：
        // a) 成功: 说明没有进行中的notify.
        // b) 在锁上等待, 最终失败: notify已经unlink完成
        //   b-1) waitMtx::try_lock成功, 等待执行wake或已执行wake, 需要等待notify完成
        //   b-2) waitMtx::try_lock失败, 不会再执行wake, 无须等待notify完成
        // c) 没上锁，直接失败：同b
        safe_unlink();

        // ------- 3.等待wake完成, 并且锁死wakeMtx, 让后续的wake调用快速返回
        wakeMtx_.lock();
        // 代码执行到这里, notify函数都已经执行完毕

        if (hasAbsTime_) {
            // 3.等待timer结束 (防止timer那边刚好处在wake之前, 还持有this指针)
            RoutineSyncTimer::getInstance().join_unschedule(timerId_);
            // 代码执行到这里, timer函数已经执行完毕
        }
    }

    int state() const { return state_; }

    RoutineSwitcherI* switcher_;
    RoutineSyncTimer::TimerId timerId_ {};
    bool hasAbsTime_ {false};
    std::atomic_bool waked_ {false};
    int delayMs_ = 1;
    int state_ {waiter_state_none};
    std::mutex wakeMtx_;
    std::atomic<RutexBase*> owner_ {nullptr};
};

template <typename IntValueType = unsigned, bool Reference = false>
struct Rutex : public IntValue<IntValueType, Reference>, public RutexBase, public DebuggerId<RutexBase>
{
public:
    typedef IntValue<IntValueType, Reference> base_t;

    Rutex() noexcept {
        // try {
        //     RoutineSyncPolicy::clsRef();
        // } catch (...) {}
    }
    ~Rutex() noexcept {}

    Rutex(const Rutex&) = delete;
    Rutex& operator=(const Rutex&) = delete;

    rutex_wait_return wait(IntValueType expectValue)
    {
        return wait_until(expectValue, RoutineSyncTimer::null_tp());
    }

    template<typename _Clock, typename _Duration>
    rutex_wait_return wait_until(IntValueType expectValue,
            const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        if (base_t::value()->load(std::memory_order_relaxed) != expectValue) {
            std::atomic_thread_fence(std::memory_order_acquire);
            RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d | check fail | %s",
                id(), __func__, !!abstime, etos(rutex_wait_return_ewouldblock));
            return rutex_wait_return_ewouldblock;
        }

        RoutineSwitcherI* switcher = &RoutineSyncPolicy::clsRef();
        std::unique_ptr<RoutineSwitcherI> raii;
        if (!switcher->valid()) {
            // exit阶段, tls对象会较早被析构
            // 此时如果处于pthread中, 可以临时new一个出来用
            if (RoutineSyncPolicy::isInPThread()) {
                switcher = new PThreadSwitcher;
                raii.reset(switcher);
            } else {
                RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d | switcher invalid && not in pthread | %s",
                    id(), __func__, !!abstime, etos(rutex_wait_return_ewouldblock));
                return rutex_wait_return_ewouldblock;
            }
        }

        RutexWaiter rw(*switcher);

        {
            std::unique_lock<std::mutex> lock(mtx_);
            if (base_t::value()->load(std::memory_order_relaxed) != expectValue) {
                RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d rw=%ld | check fail in locked | %s",
                    id(), __func__, !!abstime, rw.id(), etos(rutex_wait_return_ewouldblock));
                return rutex_wait_return_ewouldblock;
            }

            int state = rw.state();
            switch (state) {
                case RutexWaiter::waiter_state_interrupted:
                    RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d rw=%ld | state eintr in locked | %s",
                        id(), __func__, !!abstime, rw.id(), etos(rutex_wait_return_eintr));
                    return rutex_wait_return_eintr;

                case RutexWaiter::waiter_state_ready:
                    RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d rw=%ld | state ready in locked | %s",
                        id(), __func__, !!abstime, rw.id(), etos(rutex_wait_return_success));
                    return rutex_wait_return_success;
            }

            assert(!rw.safe_unlink());
            rw.mark();
            waiters_.push(&rw);
            rw.owner_.store(this, std::memory_order_relaxed);
        }

        RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d | rw=%ld | sleep",
            id(), __func__, !!abstime, rw.id());
        rw.sleep(abstime);

        RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d | rw=%ld | waked | begin join",
            id(), __func__, !!abstime, rw.id());
        rw.join();

        int state = rw.state();
        switch (state) {
            case RutexWaiter::waiter_state_none:
            case RutexWaiter::waiter_state_interrupted:
                RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d rw=%ld | joined state none or intr | %s",
                        id(), __func__, !!abstime, rw.id(), etos(rutex_wait_return_eintr));
                return rutex_wait_return_eintr;

            case RutexWaiter::waiter_state_timeout:
                RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d rw=%ld | joined state timeout | %s",
                        id(), __func__, !!abstime, rw.id(), etos(rutex_wait_return_etimeout));
                return rutex_wait_return_etimeout;
        }

        RS_DBG(dbg_rutex, "rutex=%ld | %s | abstime=%d rw=%ld | joined state ready | %s",
            id(), __func__, !!abstime, rw.id(), etos(rutex_wait_return_success));
        return rutex_wait_return_success;
    }

    int notify_one()
    {
        for (;;) {
            std::unique_lock<std::mutex> lock(mtx_);
            RutexWaiter *rw = static_cast<RutexWaiter *>(waiters_.front());
            if (!rw) {
                RS_DBG(dbg_rutex, "rutex=%ld | %s | empty list | return 0",
                    id(), __func__);
                return 0;
            }

            // 先lock，后unlink, 和join里面的顺序形成ABBA, 确保join可以等到这个函数结束 
            std::unique_lock<std::mutex> lock2(rw->wakeMtx_, std::defer_lock);
            bool isLock = lock2.try_lock();

            RS_DBG(dbg_rutex, "rutex=%ld | %s | rw=%ld try_lock=%d",
                    id(), __func__, rw->id(), isLock);

            waiters_.unlink(rw);
            rw->owner_.store(nullptr, std::memory_order_relaxed);

            if (!isLock) {
                continue;
            }

            lock.unlock();
            if (rw->wake()) {
                RS_DBG(dbg_rutex, "rutex=%ld | %s | rw=%ld wake success | return 1",
                    id(), __func__, rw->id());
                return 1;
            } else {
                RS_DBG(dbg_rutex, "rutex=%ld | %s | rw=%ld wake failed",
                    id(), __func__, rw->id());
            }
        }
    }

    int notify_all()
    {
        int n = 0;
        while (notify_one()) n++;
        return n;
    }

    template <typename OtherRutex>
    int requeue(OtherRutex* other)
    {
        RS_DBG(dbg_rutex, "rutex=%ld | %s | other=%ld",
                    id(), __func__, other->id());

        std::unique_lock<std::mutex> lock1(mtx_, std::defer_lock);
        std::unique_lock<std::mutex> lock2(other->mtx_, std::defer_lock);
        if (&mtx_ > &other->mtx_) {
            lock1.lock();
            lock2.lock();
        } else {
            lock2.lock();
            lock1.lock();
        }

        int n = 0;
        for (;;n++) {
            RutexWaiter *rw = static_cast<RutexWaiter *>(waiters_.front());
            if (!rw) {
                RS_DBG(dbg_rutex, "rutex=%ld | %s | other=%ld | done n=%d",
                    id(), __func__, other->id(), n);
                return n;
            }

            RS_DBG(dbg_rutex, "rutex=%ld | %s | other=%ld | rw=%ld",
                    id(), __func__, other->id(), rw->id());

            waiters_.unlink(rw);
            other->waiters_.push(rw);
            rw->owner_.store(other, std::memory_order_relaxed);
        }
    }
};

inline bool RutexWaiter::safe_unlink()
{
    RutexBase* owner = nullptr;
    while ((owner = owner_.load(std::memory_order_acquire))) {
        std::unique_lock<std::mutex> lock(owner->mtx_);
        if (owner == owner_.load(std::memory_order_relaxed)) {
            owner->waiters_.unlink(this);
            owner_.store(nullptr, std::memory_order_relaxed);
            return true;
        }
    }

    return false;
}


} // namespace libgo
