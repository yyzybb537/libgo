#pragma once
#include "../common/config.h"
#include "../common/clock.h"

#if USE_ROUTINE_SYNC
# include "../routine_sync/channel.h"

namespace co
{

template <typename T>
class Channel : public libgo::Channel<T>
{
    typedef libgo::Channel<T> base_type;

public:
    explicit Channel(std::size_t capacity = 0,
            std::size_t choose1 = 0,
            std::size_t choose2 = 0)
        : libgo::Channel<T>(capacity)
    {
    }

    bool TryPush(T const& t) const
    {
        return base_type::try_push(t);
    }

    bool TryPop(T & t) const
    {
        return base_type::try_pop(t);
    }

    bool TryPop(std::nullptr_t ignore) const
    {
        return base_type::try_pop(ignore);
    }

    template <typename Rep, typename Period>
    bool TimedPush(T const& t, std::chrono::duration<Rep, Period> dur) const
    {
        return base_type::push_for(t, dur);
    }

    bool TimedPush(T const& t, FastSteadyClock::time_point deadline) const
    {
        return base_type::push_until(t, deadline);
    }

    template <typename Rep, typename Period>
    bool TimedPop(T & t, std::chrono::duration<Rep, Period> dur) const
    {
        return base_type::pop_for(t, dur);
    }

    bool TimedPop(T & t, FastSteadyClock::time_point deadline) const
    {
        return base_type::pop_until(t, deadline);
    }

    template <typename Rep, typename Period>
    bool TimedPop(std::nullptr_t ignore, std::chrono::duration<Rep, Period> dur) const
    {
        return base_type::pop_for(ignore, dur);
    }

    bool TimedPop(std::nullptr_t ignore, FastSteadyClock::time_point deadline) const
    {
        return base_type::pop_until(ignore, deadline);
    }

    bool Unique() const
    {
        return base_type::unique();
    }

    void Close() const {
        base_type::close();
    }
};

} //namespace co

#else

# include "../scheduler/processer.h"
# include "../scheduler/scheduler.h"
# include "co_condition_variable.h"
# include "channel_impl.h"
# include "cas_channel_impl.h"
# include "locked_channel_impl.h"

namespace co
{

template <typename T>
class Channel
{
private:
    mutable std::shared_ptr<ChannelImpl<T>> impl_;

public:
    // @capacity: capacity of channel.
    // @choose1: use CASChannelImpl if capacity less than choose1
    // @choose2: if capacity less than choose2, use ringbuffer. else use std::list.
    explicit Channel(std::size_t capacity = 0,
            std::size_t choose1 = 0, //16,
            std::size_t choose2 = 100001)
    {
        if (capacity < choose1)
            impl_.reset(new CASChannelImpl<T>(capacity));
        else
            impl_.reset(new LockedChannelImpl<T>(capacity, capacity < choose2));
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
};

} //namespace co

#endif //USE_ROUTINE_SYNC

namespace co {

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

