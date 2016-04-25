#pragma once

#include <functional>
#include <memory>
#include "error.h"

#if USE_BOOST_COROUTINE
# include "ctx_boost_coroutine/context.h"
#elif USE_UCONTEXT
# include "ctx_ucontext/context.h"
#elif USE_FIBER
# include "ctx_win_fiber/context.h"
#else
# error "No context sets."
#endif

//namespace co
//{
//    struct ContextScopedGuard
//    {
//        ContextScopedGuard();
//        ~ContextScopedGuard();
//    };
//
//    class Context
//    {
//        class impl_t;
//
//    public:
//        explicit Context(std::size_t stack_size, std::function<void()> const& fn);
//        
//        bool SwapIn();
//
//        bool SwapOut();
//
//    private:
//        std::shared_ptr<impl_t> impl_;
//        std::size_t stack_size_;
//    };
//} //namespace co
