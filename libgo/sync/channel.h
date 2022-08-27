#pragma once
#include "../common/config.h"
#include "../common/clock.h"
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

