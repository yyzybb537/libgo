#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include "../scheduler/scheduler.h"
#include <condition_variable>

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
        impl_->Push(t, true, std::chrono::seconds(0));
        return *this;
    }

    Channel const& operator>>(T & t) const
    {
        impl_->Pop(t, true, std::chrono::seconds(0));
        return *this;
    }

    Channel const& operator>>(std::nullptr_t ignore) const
    {
        T t;
        impl_->Pop(t, true, std::chrono::seconds(0));
        return *this;
    }

    bool TryPush(T t) const
    {
        return impl_->Push(t, false, std::chrono::seconds(0));
    }

    bool TryPop(T & t) const
    {
        return impl_->Pop(t, false, std::chrono::seconds(0));
    }

    bool TryPop(std::nullptr_t ignore) const
    {
        T t;
        return impl_->Pop(t, false, std::chrono::seconds(0));
    }

    template <typename Rep, typename Period>
    bool TimedPush(T t, std::chrono::duration<Rep, Period> dur) const
    {
        return impl_->Push(t, true, dur);
    }

    bool TimedPush(T t, FastSteadyClock::time_point deadline) const
    {
        return impl_->Push(t, true, deadline - FastSteadyClock::now());
    }

    template <typename Rep, typename Period>
    bool TimedPop(T & t, std::chrono::duration<Rep, Period> dur) const
    {
        return impl_->Pop(t, true, dur);
    }

    bool TimedPop(T & t, FastSteadyClock::time_point deadline) const
    {
        return impl_->Pop(t, true, deadline - FastSteadyClock::now());
    }

    template <typename Rep, typename Period>
    bool TimedPop(std::nullptr_t ignore, std::chrono::duration<Rep, Period> dur) const
    {
        T t;
        return impl_->Pop(t, true, dur);
    }

    bool TimedPop(std::nullptr_t ignore, FastSteadyClock::time_point deadline) const
    {
        T t;
        return impl_->Pop(t, true, deadline - FastSteadyClock::now());
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

        struct Entry {
            Processer::SuspendEntry entry_;
            T * t_;
            bool * ok_;
        };
        std::queue<Entry> wQueue_;
        std::queue<Entry> rQueue_;

        // 兼容原生线程
        std::condition_variable_any wCv_;
        std::condition_variable_any rCv_;

        enum Op {
            op_read = 0,
            op_write = 1,
        };

    public:
        explicit ChannelImpl(std::size_t capacity)
            : capacity_(capacity), closed_(false), dbg_mask_(dbg_all)
        {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel init. capacity=%lu", this->getId(), capacity);
        }

        ~ChannelImpl() {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel destory.", this->getId());

            assert(lock_.try_lock());
            assert(isEmpty(wQueue_));
            assert(isEmpty(rQueue_));
        }

        void SetDbgMask(uint64_t mask) {
            dbg_mask_ = mask;
        }

        bool isEmpty(std::queue<Entry> & waitQueue) {
            while (!waitQueue.empty()) {
                auto entry = waitQueue.front();
                waitQueue.pop();

                if (entry.entry_ && entry.entry_.tk_.lock())
                    return false;
            }
            return true;
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
        template <typename Duration>
        bool Push(T t, bool bWait, Duration dur)
        {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push(bWait=%d, dur=%ld ms)", this->getId(),
                    (int)bWait, (long)std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());

            // native thread不支持Timedxxx接口
            assert(dur.count() == 0 || Processer::IsCoroutine());

            std::unique_lock<LFLock> lock(lock_);
            if (closed_) {
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return false, be closed.", this->getId());
                return false;
            }

            if (capacity_ > 0) {
                if (queue_.size() < capacity_) {
                    queue_.emplace_back(t);
                    int notified = Notify(op_read);
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return true, with capacity. size=%d, Notify=%d",
                            this->getId(), (int)queue_.size(), notified);
                    return true;
                }

                bool ok = false;
                if (bWait)
                    Wait(op_write, lock, &t, &ok, dur);

                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return %s, capacity full. size=%d",
                        this->getId(), ok ? "true" : "false", (int)queue_.size());
                return ok;
            } else {
                // 无缓冲
                if (!rQueue_.empty()) {
                    assert(queue_.empty());
                    queue_.emplace_back(t);
                    if (Notify(op_read)) {
                        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return true, zero capacity. Notified=1", this->getId());
                        return true;
                    }

                    // 没有正在读等待的协程, 写入的数据清除, 让Pop来触发
                    queue_.clear();
                }

                bool ok = false;
                if (bWait)
                    Wait(op_write, lock, &t, &ok, dur);

                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Push return %s, zero capacity.",
                        this->getId(), ok ? "true" : "false");
                return ok;
            }
        }

        // read
        template <typename Duration>
        bool Pop(T & t, bool bWait, Duration dur)
        {
            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop(bWait=%d, dur=%ld ms)", this->getId(),
                    (int)bWait, (long)std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());

            // native thread不支持Timedxxx接口
            assert(dur.count() == 0 || Processer::IsCoroutine());

            std::unique_lock<LFLock> lock(lock_);
            if (capacity_ > 0) {
                if (!queue_.empty()) {
                    t = queue_.front();
                    queue_.pop_front();
                    int notified = Notify(op_write);
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return true, with capacity. size=%d, Notify=%d",
                            this->getId(), (int)queue_.size() + 1, notified);
                    return true;
                }

                if (closed_) {
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return false, be closed.", this->getId());
                    return false;
                }

                bool ok = false;
                if (bWait)
                    Wait(op_read, lock, &t, &ok, dur);

                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return %s, capacity empty. size=%d",
                        this->getId(), ok ? "true" : "false", (int)queue_.size());
                return ok;
            } else {
                // 无缓冲
                if (closed_) {
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return false, zero capacity, be closed.", this->getId());
                    return false;
                }

                if (Notify(op_write)) {
                    assert(!queue_.empty());
                    t = queue_.front();
                    queue_.pop_front();
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return true, zero capacity. Notified=1", this->getId());
                    return true;
                }

                bool ok = false;
                if (bWait)
                    Wait(op_read, lock, &t, &ok, dur);

                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Pop return %s, zero capacity.",
                        this->getId(), ok ? "true" : "false");
                return ok;
            }
        }

        void Close()
        {
            std::unique_lock<LFLock> lock(lock_);
            if (closed_) return ;

            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel Closed. size=%d", this->getId(), (int)queue_.size());

            closed_ = true;
            std::queue<Entry> *pQueues[2] = {&rQueue_, &wQueue_};
            std::condition_variable_any *pCv[2] = {&rCv_, &wCv_};
            for (int i = 0; i < 2; ++i) {
                auto & waitQueue = *pQueues[i];
                while (!waitQueue.empty()) {
                    auto entry = waitQueue.front();
                    waitQueue.pop();

                    if (entry.ok_)
                        *entry.ok_ = false;

                    if (entry.entry_) {
                        if (Processer::Wakeup(entry.entry_)) {
                            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] close wakeup coroutine.", this->getId());
                        } else {
                            DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] close zombies coroutine.", this->getId());
                        }
                    } else {
                        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] close wakeup native thread.", this->getId());
                        pCv[i]->notify_one();
                    }
                }
            }
        }

        template <typename Duration>
        void Wait(Op op, std::unique_lock<LFLock> & lock, T * ptr, bool * ok, Duration dur)
        {
            auto & waitQueue = op == op_read ? rQueue_ : wQueue_;
            if (Processer::IsCoroutine()) {
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] channel coroutine wait. dur=%ld ns",
                        this->getId(), (long)std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count());
                if (dur.count() == 0)
                    waitQueue.push(Entry{Processer::Suspend(), ptr, ok});
                else 
                    waitQueue.push(Entry{
                            Processer::Suspend(std::chrono::duration_cast<FastSteadyClock::duration>(dur)),
                            ptr,
                            ok});

                lock.unlock();
                Processer::StaticCoYield();
            } else {
                DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] channel native thread wait. dur=%ld ms",
                        this->getId(), (long)std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
                waitQueue.push(Entry{Processer::SuspendEntry{}, ptr, ok});
                auto & cv = op == op_read ? rCv_ : wCv_;
                cv.wait(lock);
            }
        }

        int Notify(Op op)
        {
            int res = 0;
            auto & waitQueue = op == op_read ? rQueue_ : wQueue_;
            while (!waitQueue.empty()) {
                auto entry = waitQueue.front();
                waitQueue.pop();

                // read or write
                T back;
                if (op == op_read) {
                    std::swap(back, *entry.t_);
                    *entry.t_ = queue_.front();
                    *entry.ok_ = true;
                } else {
                    queue_.emplace_back(*entry.t_);
                    *entry.ok_ = true;
                }

                bool ok = false;
                if (entry.entry_) {
                    if (Processer::Wakeup(entry.entry_)) {
                        ok = true;
                        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] notify wakeup coroutine.", this->getId());
                    } else {
                        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] notify zombies coroutine.", this->getId());
                    }
                } else {
                    auto & cv = op == op_read ? rCv_ : wCv_;
                    cv.notify_one();
                    DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] notify wakeup native thread.", this->getId());
                    ok = true;
                }

                if (!ok) {
                    // rollback
                    if (op == op_read) {
                        std::swap(back, *entry.t_);
                    } else {
                        queue_.pop_back();
                    }
                    *entry.ok_ = false;
                } else {
                    // pop
                    if (op == op_read) {
                        queue_.pop_front();
                    }
                }

                if (ok) {
                    ++res;
                    break;
                }
            }
            return res;
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
