#pragma once
#include <memory>
#include <queue>
#include "block_object.h"
#include "co_mutex.h"
#include "scheduler.h"

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

    template <typename U, typename Duration>
    bool BlockTryPush(U && t, Duration const& timeout) const
    {
        int interval = 1;
        auto begin = std::chrono::high_resolution_clock::now();
        while (!TryPush(std::forward<U>(t))) {
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<Duration>(now - begin) >= timeout)
                return false;

            interval = std::min(32, interval << 1);
            g_Scheduler.SleepSwitch(interval);
        }

        return true;
    }

    template <typename U, typename Duration>
    bool BlockTryPop(U & t, Duration const& timeout) const
    {
        int interval = 1;
        auto begin = std::chrono::high_resolution_clock::now();
        while (!TryPop(t)) {
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<Duration>(now - begin) >= timeout)
                return false;

            interval = std::min(32, interval << 1);
            g_Scheduler.SleepSwitch(interval);
        }

        return true;
    }

    template <typename Duration>
    bool BlockTryPop(nullptr_t ignore, Duration const& timeout) const
    {
        int interval = 1;
        auto begin = std::chrono::high_resolution_clock::now();
        while (!TryPop(ignore)) {
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<Duration>(now - begin) >= timeout)
                return false;

            interval = std::min(32, interval << 1);
            g_Scheduler.SleepSwitch(interval);
        }

        return true;
    }

    bool Unique() const
    {
        return impl_.unique();
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

    template <typename Duration>
    bool BlockTryPush(nullptr_t ignore, Duration const& timeout) const
    {
        int interval = 1;
        auto begin = std::chrono::high_resolution_clock::now();
        while (!TryPush(ignore)) {
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<Duration>(now - begin) >= timeout)
                return false;

            interval = std::min(32, interval << 1);
            g_Scheduler.SleepSwitch(interval);
        }

        return true;
    }

    template <typename Duration>
    bool BlockTryPop(nullptr_t ignore, Duration const& timeout) const
    {
        int interval = 1;
        auto begin = std::chrono::high_resolution_clock::now();
        while (!TryPop(ignore)) {
            auto now = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<Duration>(now - begin) >= timeout)
                return false;

            interval = std::min(32, interval << 1);
            g_Scheduler.SleepSwitch(interval);
        }

        return true;
    }

    bool Unique() const
    {
        return impl_.unique();
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
