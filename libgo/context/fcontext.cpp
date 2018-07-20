#pragma once

#include "../common/config.h"
#include <memory>
#include <string.h>

#if __linux__
#include <sys/mman.h>
#endif

namespace co
{
    static stack_malloc_fn_t& StackTraits::MallocFunc()
    {
        static stack_malloc_fn_t fn = &::std::malloc;
        return fn;
    }
    static stack_free_fn_t& StackTraits::FreeFunc()
    {
        static stack_free_fn_t fn = &::std::free;
        return fn;
    }
    static int& StackTraits::GetProtectStackPageSize()
    {
        static int size = 0;
        return size;
    }
#if __linux__
    static bool StackTraits::ProtectStack(void* stack, std::size_t size, int pageSize)
    {
        if (!pageSize) return false;

        if (size <= getpagesize() * (pageSize + 1))
            return false;

        void *protect_page_addr = ((std::size_t)stack & 0xfff) ? (void*)(((std::size_t)stack & ~(std::size_t)0xfff) + 0x1000) : stack;
        if (-1 == mprotect(protect_page_addr, getpagesize() * pageSize, PROT_NONE)) {
            DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack stack error: %s",
                    stack, protect_page_addr, getpagesize(), pageSize, strerror(errno));
            return false;
        } else {
            DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack success.",
                    stack, protect_page_addr, pageSize, getpagesize());
            return true;
        }
    }
    static void StackTraits::UnprotectStack(void *stack, uint32_t pageSize)
    {
        if (!pageSize) return ;

        void *protect_page_addr = ((std::size_t)stack & 0xfff) ? (void*)(((std::size_t)stack & ~(std::size_t)0xfff) + 0x1000) : stack;
        if (-1 == mprotect(protect_page_addr, getpagesize() * pageSize, PROT_READ|PROT_WRITE)) {
            DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack stack error: %s",
                    stack, protect_page_addr, getpagesize(), pageSize, strerror(errno));
        } else {
            DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect_page:%u, protect stack success.",
                    stack, protect_page_addr, pageSize, getpagesize());
        }
    }
#else //__linux__
    static bool StackTraits::ProtectStack(void* stack, std::size_t size, int pageSize)
    {
        return false;
    }

    static void StackTraits::UnprotectStack(void *stack, uint32_t pageSize)
    {
        return ;
    }
#endif //__linux__

} //namespace co

