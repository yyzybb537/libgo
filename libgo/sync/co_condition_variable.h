#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include <list>
#include <condition_variable>
#include "wait_queue.h"

namespace co
{

/// 协程条件变量
// 1.与std::condition_variable_any的区别在于析构时不能有正在等待的协程, 否则抛异常

template <typename T>
class ConditionVariableAnyT
{
public:
    typedef std::function<void(T &)> Functor;

    enum class cv_status { no_timeout, timeout, no_queued };

private:
    // 兼容原生线程
    struct NativeThreadEntry
    {
        std::mutex mtx;

        std::condition_variable_any cv;

        LFLock2 notified;
    };

    enum eSuspendFlag
    {
        suspend_begin = 0x1,
        suspend_end = 0x2,
        wakeup_begin = 0x4,
        wakeup_end = 0x8,
    };

    struct Entry : public WaitQueueHook, public RefObject
    {
        // 控制是否超时的标志位
        LFLock noTimeoutLock;

        atomic_t<int> suspendFlags {0};

        Processer::SuspendEntry coroEntry;

        T value;

        NativeThreadEntry* nativeThreadEntry;

        bool isWaiting;

        Entry() : value(), nativeThreadEntry(nullptr), isWaiting(true) {}
        ~Entry() {
            if (nativeThreadEntry) {
                delete nativeThreadEntry;
                nativeThreadEntry = nullptr;
            }
        }

        bool notify(Functor const func) {
            DebugPrint(dbg_channel, "cv::notify ->");
            for (;;) {
                int flag = suspendFlags.load(std::memory_order_relaxed);

                if (flag & eSuspendFlag::suspend_begin) {
                    DebugPrint(dbg_channel, "cv::notify -> flag = suspend_begin");
                    // 已在挂起中
                    while ((suspendFlags.load(std::memory_order_acquire) & eSuspendFlag::suspend_end) == 0);
                    break;
                }

                // 还未挂起, 可以直接唤醒
                if (suspendFlags.compare_exchange_weak(flag,
                            flag | eSuspendFlag::wakeup_begin,
                            std::memory_order_acq_rel, std::memory_order_relaxed))
                {
                    DebugPrint(dbg_channel, "cv::notify -> wakeup complete");

                    flag |= eSuspendFlag::wakeup_begin | eSuspendFlag::wakeup_end;

                    if (!noTimeoutLock.try_lock()) {
                        assert(false);
                        suspendFlags.store(flag, std::memory_order_release);
                        DebugPrint(dbg_channel, "cv::notify -> wakeup_end");
                        return false;;
                    }

                    if (func)
                        func(value);

                    suspendFlags.store(flag, std::memory_order_release);
                    DebugPrint(dbg_channel, "cv::notify -> wakeup_end");
                    return true;
                }
            }

            DebugPrint(dbg_channel, "cv::notify -> wakeup");

            // coroutine
            if (!nativeThreadEntry) {
                if (!noTimeoutLock.try_lock())
                    return false;;

                if (Processer::Wakeup(coroEntry, [&]{ if (func) func(value); })) {
//                    DebugPrint(dbg_channel, "notify %d.", value.id);
                    return true;
                }

                return false;;
            }

            // native thread
            std::unique_lock<std::mutex> threadLock(nativeThreadEntry->mtx);
            if (!noTimeoutLock.try_lock())
                return false;;

            if (func)
                func(value);
            nativeThreadEntry->cv.notify_one();
            return true;
        }
    };

    WaitQueue<Entry> queue_;

    bool relockAfterWait_ = true;

    template <typename LockType>
    struct AutoLock
    {
        LockType* lock_;
        bool bLock_;

        AutoLock(LockType & lock, bool bLock) : lock_(&lock), bLock_(bLock) {}
        ~AutoLock() { if (bLock_) lock_->lock(); }
    };

public:
    typedef typename WaitQueue<Entry>::CondRet CondRet;

public:
    explicit ConditionVariableAnyT(size_t nonblockingCapacity = 0,
            Functor convertToNonblockingFunctor = NULL)
        : queue_(&isValid, nonblockingCapacity,
                [=](Entry *entry)
                {
                    if (entry->notify(convertToNonblockingFunctor)) {
                        entry->isWaiting = false;
                        return true;
                    }
                    return false;
                })
    {
    }
    ~ConditionVariableAnyT() {}

    void setRelockAfterWait(bool b) { relockAfterWait_ = b; }

    template <typename LockType>
    cv_status wait(LockType & lock,
            T value = T(),
            std::function<CondRet(size_t)> const& cond = NULL)
    {
        std::chrono::seconds* time = nullptr;
        return do_wait(lock, time, value, cond);
    }

    template <typename LockType, typename TimeDuration>
    cv_status wait_for(LockType & lock,
            TimeDuration duration,
            T value = T(),
            std::function<CondRet(size_t)> const& cond = NULL)
    {
        return do_wait(lock, &duration, value, cond);
    }

    template <typename LockType, typename TimePoint>
    cv_status wait_util(LockType & lock,
            TimePoint timepoint,
            T value = T(),
            std::function<CondRet(size_t)> const& cond = NULL)
    {
        return do_wait(lock, &timepoint, value, cond);
    }

    bool notify_one(Functor const& func = NULL)
    {
        Entry* entry = nullptr;
        while (queue_.pop(entry)) {
            AutoRelease<Entry> pEntry(entry);

            if (!entry->isWaiting) {
                if (func)
                    func(entry->value);
                return true;
            }

            if (entry->notify(func))
                return true;
        }

        return false;
    }

    size_t notify_all(Functor const& func = NULL)
    {
        size_t n = 0;
        while (notify_one(func))
            ++n;
        return n;
    }

    bool empty() {
        return queue_.empty();
    }

    bool size() {
        return queue_.size();
    }

private:
    template <typename TimeType>
    inline void coroSuspend(Processer::SuspendEntry & coroEntry, TimeType * time)
    {
        if (time)
            coroEntry = Processer::Suspend(*time);
        else
            coroEntry = Processer::Suspend();
    }

    template <typename LockType, typename Rep, typename Period>
    inline void threadSuspend(std::condition_variable_any & cv,
            LockType & lock, std::chrono::duration<Rep, Period> * dur)
    {
        if (dur)
            cv.wait_for(lock, *dur);
        else
            cv.wait(lock);
    }

    template <typename LockType, typename Clock, typename Duration>
    inline void threadSuspend(std::condition_variable_any & cv,
            LockType & lock, std::chrono::time_point<Clock, Duration> * tp)
    {
        if (tp)
            cv.wait_until(lock, *tp);
        else
            cv.wait(lock);
    }

    template <typename LockType, typename TimeType>
    cv_status do_wait(LockType & lock,
            TimeType* time, T value = T(),
            std::function<CondRet(size_t)> const& cond = NULL)
    {
        cv_status result;
        Entry *entry = new Entry;
        AutoRelease<Entry> pEntry(entry);
        entry->value = value;
        size_t qSize = 0;
        auto ret = queue_.push(entry, [&](size_t queueSize){
                CondRet ret{true, true};
                if (cond) {
                    ret = cond(queueSize);
                    if (!ret.canQueue)
                        return ret;
                }

                entry->IncrementRef();

                if (!ret.needWait) {
                    entry->isWaiting = false;
                    return ret;
                }

                qSize = queueSize;
                return ret;
                });

        if (!ret.canQueue) {
            return cv_status::no_queued;
        }

        if (!ret.needWait) {
            return cv_status::no_timeout;
        }

        lock.unlock();

        AutoLock<LockType> autoLock(lock, relockAfterWait_);

        DebugPrint(dbg_channel, "cv::wait ->");

        int spinA = 0;
        int spinB = 0;
        int flag = 0;
        for (;;) {
            flag = entry->suspendFlags.load(std::memory_order_relaxed);

            if (flag & eSuspendFlag::wakeup_begin) {
                DebugPrint(dbg_channel, "cv::wait -> flag = wakeup_begin");
                // 已在被唤醒
                while ((entry->suspendFlags.load(std::memory_order_acquire) & eSuspendFlag::wakeup_end) == 0);
                return cv_status::no_timeout;
            } else {
                // 无人唤醒, 先自旋等一等再真正挂起
                if (++spinA <= 0) {
                    continue;
                }

                if (Processer::IsCoroutine()) {
//                    if (++spinB <= 1 << (4 - (std::min)((size_t)4, qSize))) {
                    if (++spinB <= 1) {
                        Processer::StaticCoYield();
                        continue;
                    }
                } else {
                    if (++spinB <= 8) {
                        std::this_thread::yield();
                        continue;
                    }
                }
            }

            if (entry->suspendFlags.compare_exchange_weak(flag,
                        flag | eSuspendFlag::suspend_begin,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                break;
        }

        flag |= eSuspendFlag::suspend_begin | eSuspendFlag::suspend_end;

        DebugPrint(dbg_channel, "cv::wait -> suspend_begin");
        if (Processer::IsCoroutine()) {
            // 协程
            coroSuspend(entry->coroEntry, time);
            entry->suspendFlags.store(flag, std::memory_order_release);   // release
            DebugPrint(dbg_channel, "cv::wait -> suspend_end");
            Processer::StaticCoYield();
            result = entry->noTimeoutLock.try_lock() ?
                cv_status::timeout :
                cv_status::no_timeout;
        } else {
            // 原生线程
            entry->nativeThreadEntry = new NativeThreadEntry;
            std::unique_lock<std::mutex> threadLock(entry->nativeThreadEntry->mtx);
            entry->suspendFlags.store(flag, std::memory_order_release);   // release
            DebugPrint(dbg_channel, "cv::wait -> suspend_end");
            threadSuspend(entry->nativeThreadEntry->cv, threadLock, time);
            result = entry->noTimeoutLock.try_lock() ?
                cv_status::timeout :
                cv_status::no_timeout;
        }
        //如果超时，那么发送notify清除queue中的entry
        if (result == cv_status::timeout) {
            notify_one();
        }
        return result;
    }

    static bool isValid(Entry* entry) {
        if (!entry->isWaiting) return true;
        if ((entry->suspendFlags & eSuspendFlag::suspend_begin) == 0) return true;
        if (!entry->nativeThreadEntry)
            return !entry->coroEntry.IsExpire();
        return entry->nativeThreadEntry->notified.is_lock();
    }
};

typedef ConditionVariableAnyT<bool> ConditionVariableAny;

} //namespace co
