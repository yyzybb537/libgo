#pragma once

#include <libgo/config.h>
#include <functional>
#include <memory>
#include <libgo/error.h>
#include <string.h>

#if __linux__
#include <sys/mman.h>
#endif

namespace co
{
    struct StackAllocator
    {
        static stack_malloc_fn_t& get_malloc_fn()
        {
            static stack_malloc_fn_t stack_malloc_fn = &::std::malloc;
            return stack_malloc_fn;
        }
        static stack_free_fn_t& get_free_fn()
        {
            static stack_free_fn_t stack_free_fn = &::std::free;
            return stack_free_fn;
        }
        static uint32_t& get_protect_stack_page()
        {
            static uint32_t protect_stack_page = 0;
            return protect_stack_page;
        }
#if __linux__
        static bool protect_stack(void *top, std::size_t stack_size,
                uint32_t page)
        {
            if (!page) return false;

            if (stack_size <= getpagesize() * (page + 1))
                ThrowError(eCoErrorCode::ec_protect_stack_failed);

            void *protect_page_addr = ((std::size_t)top & 0xfff) ? (void*)(((std::size_t)top & ~(std::size_t)0xfff) + 0x1000) : top;
            if (-1 == mprotect(protect_page_addr, getpagesize() * page, PROT_NONE)) {
                DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack top error: %s",
                        top, protect_page_addr, getpagesize(), page, strerror(errno));
                return false;
            } else {
                DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack success.",
                        top, protect_page_addr, page, getpagesize());
                return true;
            }
        }
        static void unprotect_stack(void *top, uint32_t page)
        {
            if (!page) return ;

            void *protect_page_addr = ((std::size_t)top & 0xfff) ? (void*)(((std::size_t)top & ~(std::size_t)0xfff) + 0x1000) : top;
            if (-1 == mprotect(protect_page_addr, getpagesize() * page, PROT_READ|PROT_WRITE)) {
                DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack top error: %s",
                        top, protect_page_addr, getpagesize(), page, strerror(errno));
            } else {
                DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack success.",
                        top, protect_page_addr, page, getpagesize());
            }
        }
#endif
    };

} //namespace co

#if USE_BOOST_COROUTINE
# include <libgo/ctx_boost_coroutine/context.h>
#elif USE_BOOST_CONTEXT
# include <libgo/ctx_boost_context/context.h>
#elif USE_UCONTEXT
# include <libgo/ctx_ucontext/context.h>
#elif USE_FIBER
# include <libgo/ctx_win_fiber/context.h>
#else
# error "No context sets."
#endif
