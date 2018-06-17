#pragma once

#include <deque>

namespace co
{
    template <typename T, typename Alloc = std::allocator<T>>
    using Deque = std::deque<T, Alloc>;

    // TODO: 实现多读一写线程安全的deque

} // namespace co
