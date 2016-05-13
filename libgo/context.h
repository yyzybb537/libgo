#pragma once

#include <functional>
#include <memory>
#include "error.h"

namespace co
{
    typedef void*(*stack_malloc_fn_t)(size_t size);
    typedef void(*stack_free_fn_t)(void *ptr);

    struct StackAllocator
    {
        inline static stack_malloc_fn_t& get_malloc_fn()
        {
            static stack_malloc_fn_t stack_malloc_fn = &::std::malloc;
            return stack_malloc_fn;
        }
        inline static stack_free_fn_t& get_free_fn()
        {
            static stack_free_fn_t stack_free_fn = &::std::free;
            return stack_free_fn;
        }
    };

} //namespace co

#if USE_BOOST_COROUTINE
# include "ctx_boost_coroutine/context.h"
#elif USE_UCONTEXT
# include "ctx_ucontext/context.h"
#elif USE_FIBER
# include "ctx_win_fiber/context.h"
#else
# error "No context sets."
#endif
