#pragma once
#include <memory>
#include <queue>
#include <libgo/block_object.h>
#include <libgo/co_mutex.h>
#include <libgo/scheduler.h>

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
    ~Channel()
    {
    }

    template <typename U>
    Channel const& operator<<(U && t) const
    {
        (*impl_) << std::forward<U>(t);
        return *this;
    }

    template <typename U>
    Channel const& operator>>(U & t) const
    {
        (*impl_) >> t;
        return *this;
    }

    Channel const& operator>>(nullptr_t ignore) const
    {
        (*impl_) >> ignore;
        return *this;
    }

    template <typename U>
    bool TryPush(U && t) const
    {
        return impl_->TryPush(std::forward<U>(t));
    }

    template <typename U>
    bool TryPop(U & t) const
    {
        return impl_->TryPop(t);
    }

    bool TryPop(nullptr_t ignore) const
    {
        return impl_->TryPop(ignore);
    }

    template <typename U, typename DurationOrDeadline>
    bool TimedPush(U && t, DurationOrDeadline const& dd) const
    {
        return impl_->TimedPush(std::forward<U>(t), dd);
    }

    template <typename U, typename DurationOrDeadline>
    bool TimedPop(U & t, DurationOrDeadline const& dd) const
    {
        return impl_->TimedPop(t, dd);
    }

    template <typename DurationOrDeadline>
    bool TimedPop(nullptr_t ignore, DurationOrDeadline const& dd) const
    {
        return impl_->TimedPop(ignore, dd);
    }

    bool Unique() const
    {
        return impl_.unique();
    }

    bool empty() const
    {
        return impl_->empty();
    }

    std::size_t size() const
    {
        return impl_->size();
    }

private:
    class ChannelImpl
    {
        BlockObject write_block_;
        BlockObject read_block_;
        std::queue<T> queue_;
        CoMutex queue_lock_;

    public:
        explicit ChannelImpl(std::size_t capacity)
            : write_block_(capacity)
        {}

        bool empty()
        {
            return !read_block_.IsWakeup();
        }

        std::size_t size()
        {
            return queue_.size();
        }

        // write
        template <typename U>
        void operator<<(U && t)
        {
            write_block_.CoBlockWait();

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                queue_.push(std::forward<U>(t));
            }

            read_block_.Wakeup();
        }

        // read
        template <typename U>
        void operator>>(U & t)
        {
            write_block_.Wakeup();
            read_block_.CoBlockWait();

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                t = std::move(queue_.front());
                queue_.pop();
            }
        }

        // read and ignore
        void operator>>(nullptr_t ignore)
        {
            write_block_.Wakeup();
            read_block_.CoBlockWait();

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                queue_.pop();
            }
        }

        // write
        template <typename U, typename DurationOrDeadline>
        bool TimedPush(U && t, DurationOrDeadline const& dd)
        {
			if (!write_block_.CoBlockWaitTimed(dd))
                return false;

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                queue_.push(std::forward<U>(t));
            }

            read_block_.Wakeup();
            return true;
        }

        // read
        template <typename U, typename DurationOrDeadline>
        bool TimedPop(U & t, DurationOrDeadline const& dd)
        {
            write_block_.Wakeup();
			if (!read_block_.CoBlockWaitTimed(dd))
            {
                if (write_block_.TryBlockWait())
                    return false;
                else
                {
                    while (!read_block_.TryBlockWait())
                        if (write_block_.TryBlockWait())
                            return false;
                        else
                            g_Scheduler.CoYield();
                }
            }

            std::unique_lock<CoMutex> lock(queue_lock_);
            t = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        // read and ignore
        template <typename DurationOrDeadline>
        bool TimedPop(nullptr_t ignore, DurationOrDeadline const& dd)
        {
            write_block_.Wakeup();
            if (!read_block_.CoBlockWaitTimed(dd))
            {
                if (write_block_.TryBlockWait())
                    return false;
                else
                {
                    while (!read_block_.TryBlockWait())
                        if (write_block_.TryBlockWait())
                            return false;
                        else
                            g_Scheduler.CoYield();
                }
            }

            std::unique_lock<CoMutex> lock(queue_lock_);
            queue_.pop();
            return true;
        }


        // try write
        template <typename U>
        bool TryPush(U && t)
        {
            if (!write_block_.TryBlockWait())
                return false;

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                queue_.push(std::forward<U>(t));
            }

            read_block_.Wakeup();
            return true;
        }

        // try read
        template <typename U>
        bool TryPop(U & t)
        {
            write_block_.Wakeup();
            while (!read_block_.TryBlockWait())
                if (write_block_.TryBlockWait())
                    return false;
                else
                    g_Scheduler.CoYield();

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                t = std::move(queue_.front());
                queue_.pop();
            }
            return true;
        }

        // try read and ignore
        bool TryPop(nullptr_t ignore)
        {
            write_block_.Wakeup();
            while (!read_block_.TryBlockWait())
                if (write_block_.TryBlockWait())
                    return false;
                else
                    g_Scheduler.CoYield();

            {
                std::unique_lock<CoMutex> lock(queue_lock_);
                queue_.pop();
            }
            return true;
        }
    };


};


template <>
class Channel<void>
{
private:
    class ChannelImpl;
    mutable std::shared_ptr<ChannelImpl> impl_;

public:
    explicit Channel(std::size_t capacity = 0)
    {
        impl_.reset(new ChannelImpl(capacity));
    }
    ~Channel()
    {
    }

    Channel const& operator<<(nullptr_t ignore) const
    {
        (*impl_) << ignore;
        return *this;
    }

    Channel const& operator>>(nullptr_t ignore) const
    {
        (*impl_) >> ignore;
        return *this;
    }

    bool TryPush(nullptr_t ignore) const
    {
        return impl_->TryPush(ignore);
    }

    bool TryPop(nullptr_t ignore) const
    {
        return impl_->TryPop(ignore);
    }

    template <typename DurationOrDeadline>
    bool TimedPush(nullptr_t ignore, DurationOrDeadline const& dd) const
    {
        return impl_->TimedPush(ignore, dd);
    }

    template <typename DurationOrDeadline>
    bool TimedPop(nullptr_t ignore, DurationOrDeadline const& dd) const
    {
        return impl_->TimedPop(ignore, dd);
    }

    bool Unique() const
    {
        return impl_.unique();
    }

    bool empty() const
    {
        return impl_->empty();
    }

    std::size_t size() const
    {
        return 0;
    }

private:
    class ChannelImpl
    {
        BlockObject write_block_;
        BlockObject read_block_;

    public:
        explicit ChannelImpl(std::size_t capacity)
            : write_block_(capacity)
        {}

        bool empty()
        {
            return !read_block_.IsWakeup();
        }

        // write
        void operator<<(nullptr_t ignore)
        {
            write_block_.CoBlockWait();
            read_block_.Wakeup();
        }

        // read and ignore
        void operator>>(nullptr_t ignore)
        {
            write_block_.Wakeup();
            read_block_.CoBlockWait();
        }

        // write
        template <typename DurationOrDeadline>
        bool TimedPush(nullptr_t ignore, DurationOrDeadline const& dd)
        {
			if (!write_block_.CoBlockWaitTimed(dd))
                return false;

            read_block_.Wakeup();
            return true;
        }

        // read
        template <typename DurationOrDeadline>
        bool TimedPop(nullptr_t ignore, DurationOrDeadline const& dd)
        {
            write_block_.Wakeup();
			if (!read_block_.CoBlockWaitTimed(dd))
            {
                if (write_block_.TryBlockWait())
                    return false;
                else
                {
                    while (!read_block_.TryBlockWait())
                        if (write_block_.TryBlockWait())
                            return false;
                        else
                            g_Scheduler.CoYield();
                }
            }

            return true;
        }

        // try write
        bool TryPush(nullptr_t ignore)
        {
            if (!write_block_.TryBlockWait())
                return false;

            read_block_.Wakeup();
            return true;
        }

        // try read and ignore
        bool TryPop(nullptr_t ignore)
        {
            write_block_.Wakeup();
            while (!read_block_.TryBlockWait())
                if (write_block_.TryBlockWait())
                    return false;
                else
                    g_Scheduler.CoYield();

            return true;
        }
    };
};

} //namespace co
