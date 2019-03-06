#pragma once
#include "../common/config.h"
#include "channel_impl.h"
#include "ringbuffer.h"
#include <list>

namespace co
{

template <typename T>
class LockedChannelImpl : public ChannelImpl<T>
{
    typedef std::mutex lock_t;
    typedef FastSteadyClock::time_point time_point_t;

    lock_t lock_;
    const std::size_t capacity_;
    bool closed_;
    uint64_t dbg_mask_;

    bool useRingBuffer_;
    RingBuffer<T> q_;
    std::list<T> lq_;

    typedef ConditionVariableAnyT<T*> wait_queue_t;
    wait_queue_t wq_;
    wait_queue_t rq_;

public:
    explicit LockedChannelImpl(std::size_t capacity, bool useRingBuffer)
        : capacity_(capacity), closed_(false), dbg_mask_(dbg_all)
        , useRingBuffer_(useRingBuffer)
        , q_(useRingBuffer ? capacity : 1)
    {
        wq_.setRelockAfterWait(false);
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel init. capacity=%lu", this->getId(), capacity);
    }

    bool push(T const& t) {
        if (useRingBuffer_)
            return q_.push(t);
        else {
            if (lq_.size() >= capacity_)
                return false;

            lq_.emplace_back(t);
            return true;
        }
    }

    bool pop(T & t) {
        if (useRingBuffer_)
            return q_.pop(t);
        else {
            if (lq_.empty())
                return false;

            t = lq_.front();
            lq_.pop_front();
            return true;
        }
    }
    
    // write
    bool Push(T t, bool bWait, FastSteadyClock::time_point deadline = FastSteadyClock::time_point{})
    {
        DebugPrint(dbg_channel, "[id=%ld] Push ->", this->getId());

        if (closed_) return false;
        std::unique_lock<lock_t> lock(lock_);
        if (closed_) return false;

        if (!capacity_ && rq_.notify_one([&](T* p){ *p = t; })) {
            DebugPrint(dbg_channel, "[id=%ld] Push Notify", this->getId());
            return true;
        }

        if (capacity_ > 0 && push(t)) {
            if (Size() == 1) {
                if (rq_.notify_one([&](T* p){ pop(*p); })) {
                    DebugPrint(dbg_channel, "[id=%ld] Push Notify", this->getId());
                }
            }
            DebugPrint(dbg_channel, "[id=%ld] Push complete queued", this->getId());
            return true;
        }

        if (!bWait) {
            DebugPrint(dbg_channel, "[id=%ld] TryPush failed.", this->getId());
            return false;
        }

        DebugPrint(dbg_channel, "[id=%ld] Push wait", this->getId());

        typename wait_queue_t::cv_status cv_status;
        if (deadline == time_point_t())
            cv_status = wq_.wait(lock, &t);
        else
            cv_status = wq_.wait_util(lock, deadline, &t);

        switch ((int)cv_status) {
            case (int)wait_queue_t::cv_status::no_timeout:
                if (closed_) {
                    DebugPrint(dbg_channel, "[id=%ld] Push failed by closed.", this->getId());
                    return false;
                }

                DebugPrint(dbg_channel, "[id=%ld] Push complete.", this->getId());
                return true;

            case (int)wait_queue_t::cv_status::timeout:
                DebugPrint(dbg_channel, "[id=%ld] Push timeout.", this->getId());
                return false;

            default:
                assert(false);
        }

        return false;
    }

    // read
    bool Pop(T & t, bool bWait, FastSteadyClock::time_point deadline = FastSteadyClock::time_point{})
    {
        DebugPrint(dbg_channel, "[id=%ld] Pop ->", this->getId());

        if (closed_) return false;
        std::unique_lock<lock_t> lock(lock_);
        if (closed_) return false;

        if (capacity_ > 0) {
            if (pop(t)) {
                if (Size() == capacity_ - 1) {
                    if (wq_.notify_one([&](T* p){ push(*p); })) {
                        DebugPrint(dbg_channel, "[id=%ld] Pop Notify size=%lu.", this->getId(), Size());
                    }
                }
                DebugPrint(dbg_channel, "[id=%ld] Pop complete unqueued.", this->getId());
                return true;
            }
        } else {
            if (wq_.notify_one([&](T* p){ t = *p; })) {
                DebugPrint(dbg_channel, "[id=%ld] Pop Notify ...", this->getId());
                return true;
            }
        }

        if (!bWait) {
            DebugPrint(dbg_channel, "[id=%ld] TryPop failed.", this->getId());
            return false;
        }

        DebugPrint(dbg_channel, "[id=%ld] Pop wait.", this->getId());

        typename wait_queue_t::cv_status cv_status;
        if (deadline == time_point_t())
            cv_status = rq_.wait(lock, &t);
        else
            cv_status = rq_.wait_util(lock, deadline, &t);

        switch ((int)cv_status) {
            case (int)wait_queue_t::cv_status::no_timeout:
                if (closed_) {
                    DebugPrint(dbg_channel, "[id=%ld] Pop failed by closed.", this->getId());
                    return false;
                }

                DebugPrint(dbg_channel, "[id=%ld] Pop complete.", this->getId());
                return true;

            case (int)wait_queue_t::cv_status::timeout:
                DebugPrint(dbg_channel, "[id=%ld] Pop timeout.", this->getId());
                return false;

            default:
                assert(false);
        }

        return false;
    }

    ~LockedChannelImpl() {
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel destory.", this->getId());

        assert(lock_.try_lock());
    }

    void SetDbgMask(uint64_t mask) {
        dbg_mask_ = mask;
    }

    bool Empty()
    {
        return Size() == 0;
    }

    std::size_t Size()
    {
        return useRingBuffer_ ? q_.size() : lq_.size();
    }

    void Close()
    {
        std::unique_lock<lock_t> lock(lock_);
        if (closed_) return ;

        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel Closed. size=%d", this->getId(), (int)Size());

        closed_ = true;
        rq_.notify_all();
        wq_.notify_all();
    }
};

} //namespace co
