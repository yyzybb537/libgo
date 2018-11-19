#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include "../scheduler/scheduler.h"
#include "co_condition_variable.h"

namespace co
{

template <typename T>
class Channel
{
private:
    class ChannelImpl;
    mutable std::shared_ptr<ChannelImpl> impl_;

public:
    explicit Channel(std::size_t capacity = 0)
    {
        impl_.reset(new ChannelImpl(capacity));
    }

    void SetDbgMask(uint64_t mask)
    {
        impl_->SetDbgMask(mask);
    }

    Channel const& operator<<(T t) const
    {
        impl_->Push(t, true);
        return *this;
    }

    Channel const& operator>>(T & t) const
    {
        impl_->Pop(t, true);
        return *this;
    }

    Channel const& operator>>(std::nullptr_t ignore) const
    {
        T t;
        impl_->Pop(t, true);
        return *this;
    }

    bool TryPush(T t) const
    {
        return impl_->Push(t, false);
    }

    bool TryPop(T & t) const
    {
        return impl_->Pop(t, false);
    }

    bool TryPop(std::nullptr_t ignore) const
    {
        T t;
        return impl_->Pop(t, false);
    }

    template <typename Rep, typename Period>
    bool TimedPush(T t, std::chrono::duration<Rep, Period> dur) const
    {
        return impl_->Push(t, true, dur + FastSteadyClock::now());
    }

    bool TimedPush(T t, FastSteadyClock::time_point deadline) const
    {
        return impl_->Push(t, true, deadline);
    }

    template <typename Rep, typename Period>
    bool TimedPop(T & t, std::chrono::duration<Rep, Period> dur) const
    {
        return impl_->Pop(t, true, dur + FastSteadyClock::now());
    }

    bool TimedPop(T & t, FastSteadyClock::time_point deadline) const
    {
        return impl_->Pop(t, true, deadline);
    }

    template <typename Rep, typename Period>
    bool TimedPop(std::nullptr_t ignore, std::chrono::duration<Rep, Period> dur) const
    {
        T t;
        return impl_->Pop(t, true, dur + FastSteadyClock::now());
    }

    bool TimedPop(std::nullptr_t ignore, FastSteadyClock::time_point deadline) const
    {
        T t;
        return impl_->Pop(t, true, deadline);
    }

    bool Unique() const
    {
        return impl_.unique();
    }

    void Close() const {
        impl_->Close();
    }

    // ------------- 兼容旧版接口
    bool empty() const
    {
        return impl_->Empty();
    }

    std::size_t size() const
    {
        return impl_->Size();
    }

private:
    class ChannelImpl : public IdCounter<ChannelImpl>
    {
        LFLock lock_;
        std::size_t capacity_;
        bool closed_;
        std::deque<T> queue_;
        uint64_t dbg_mask_;

        // 兼容原生线程
        ConditionVariableAny wCv_;
        ConditionVariableAny rCv_;

    public:
        explicit ChannelImpl(std::size_t capacity)
            : capacity_(capacity), closed_(false), dbg_mask_(dbg_all)
        {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel init. capacity=%lu", this->getId(), capacity);
        }

        ~ChannelImpl() {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel destory.", this->getId());

            assert(lock_.try_lock());
        }

        void SetDbgMask(uint64_t mask) {
            dbg_mask_ = mask;
        }

        bool Empty()
        {
            std::unique_lock<LFLock> lock(lock_);
            return queue_.empty();
        }

        std::size_t Size()
        {
            std::unique_lock<LFLock> lock(lock_);
            return queue_.size();
        }

        // write
        bool Push(T t, bool bWait, FastSteadyClock::time_point deadline = FastSteadyClock::time_point{})
        {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push(bWait=%d, dur=%ld ms)", this->getId(),
                    (int)bWait,
                    deadline == FastSteadyClock::time_point{} ? 0 :
                    (long)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - FastSteadyClock::now()).count());

            std::unique_lock<LFLock> lock(lock_);
retry:
            if (closed_) {
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return false, be closed.", this->getId());
                return false;
            }

            if (capacity_ > 0) {
                if (queue_.size() < capacity_) {
                    queue_.emplace_back(t);
                    bool notified = rCv_.notify_one();
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return true, with capacity. size=%d, Notify=%d",
                            this->getId(), (int)queue_.size(), (int)notified);
                    return true;
                }
            } else {
                // 无缓冲
                if (rCv_.notify_one()) {
                    queue_.emplace_back(t);
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return true, zero capacity. Notified=1", this->getId());
                    return true;
                }
            }
                
            if (bWait && (deadline == FastSteadyClock::time_point{} || deadline > FastSteadyClock::now())) {
                auto fn = [this, t]{
                    queue_.emplace_back(t);
                };

                if (deadline == FastSteadyClock::time_point{}) {
                    if (wCv_.wait(lock, fn) == std::cv_status::no_timeout)
                        return true;
                } else {
                    if (wCv_.wait_until(lock, deadline, fn) == std::cv_status::no_timeout)
                        return true;
                }

                goto retry;
            }

            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return false, capacity size=%d",
                    this->getId(), (int)queue_.size());
            return false;
        }

        // read
        bool Pop(T & t, bool bWait, FastSteadyClock::time_point deadline = FastSteadyClock::time_point{})
        {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop(bWait=%d, dur=%ld ms)", this->getId(),
                    (int)bWait,
                    deadline == FastSteadyClock::time_point{} ? 0 :
                    (long)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - FastSteadyClock::now()).count());

            std::unique_lock<LFLock> lock(lock_);
retry:
            if (!queue_.empty()) {
                t = queue_.front();
                queue_.pop_front();
                int notified = wCv_.notify_one();
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return true, with capacity. size=%d, Notify=%d",
                        this->getId(), (int)queue_.size() + 1, notified);
                return true;
            }

            if (closed_) {
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return false, be closed.", this->getId());
                return false;
            }
            
            // 无缓冲
            if (wCv_.notify_one()) {
                t = queue_.front();
                queue_.pop_front();
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return true, Zero capacity. Notified=1", this->getId());
                return true;
            }

            if (bWait && (deadline == FastSteadyClock::time_point{} || deadline > FastSteadyClock::now())) {
                if (deadline == FastSteadyClock::time_point{}) {
                    rCv_.wait(lock);
                    goto retry;
                } else {
                    if (rCv_.wait_until(lock, deadline) == std::cv_status::no_timeout)
                        goto retry;
                }
            }

            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return false.", this->getId());
            return false;
        }

        void Close()
        {
            std::unique_lock<LFLock> lock(lock_);
            if (closed_) return ;

            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel Closed. size=%d", this->getId(), (int)queue_.size());

            closed_ = true;
            wCv_.notify_all();
            rCv_.notify_all();
        }
    };
};


template <>
class Channel<void> : public Channel<std::nullptr_t>
{
public:
    explicit Channel(std::size_t capacity = 0)
        : Channel<std::nullptr_t>(capacity)
    {}
};

template <typename T>
using co_chan = Channel<T>;

} //namespace co
