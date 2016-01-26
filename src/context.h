#pragma once

#include <functional>
#include <memory>

namespace co
{
    struct ContextScopedGuard
    {
        ContextScopedGuard();
        ~ContextScopedGuard();
    };

    class Context
    {
        class impl_t;

    public:
        explicit Context(std::size_t stack_size);

        bool Init(std::function<void()> const& fn, char* shared_stack, uint32_t shared_stack_cap);
        
        bool SwapIn();

        bool SwapOut();

    private:
        std::shared_ptr<impl_t> impl_;
        std::size_t stack_size_;
    };

} //namespace co
