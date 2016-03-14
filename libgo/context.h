#pragma once

#include <functional>
#include <memory>
#include "error.h"

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
        explicit Context(std::size_t stack_size, std::function<void()> const& fn);

        bool Init(char* shared_stack, uint32_t shared_stack_cap);
        
        bool SwapIn();

        bool SwapOut();

    private:
        std::shared_ptr<impl_t> impl_;
        std::size_t stack_size_;
    };

} //namespace co
