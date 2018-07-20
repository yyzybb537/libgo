#pragma once
#include "../common/config.h"
#include "fcontext.h"

namespace co {

class Context
{
public:
    Context(fn_t fn, intptr_t vp, std::size_t stackSize)
        : fn_(fn), stackSize_(stackSize)
    {
        stack_ = (char*)StackAllocator::get_malloc_fn()(stackSize_);
        DebugPrint(dbg_task, "valloc stack. size=%u ptr=%p",
                stackSize_, stack_);

        ctx_ = make_fcontext(stack_, stackSize_, fn_);

        uint32_t protectPage = GetProtectStackPageSize();
        if (protectPage && UnprotectStack(stack_, stackSize_, protectPage))
            protectPage_ = protectPage;
    }
    ~Context()
    {
        if (stack_) {
            DebugPrint(dbg_task, "free stack. ptr=%p", stack_);
            if (protectPage_)
                UnprotectStack(stack_, stackSize_, protectPage_);
            StackAllocator::get_free_fn()(stack_);
            stack_ = NULL;
        }
    }

    ALWAYS_INLINE void SwapIn()
    {
        jump_fcontext(&GetTlsContext(), ctx_, vp_);
    }

    ALWAYS_INLINE void SwapTo(Context & other)
    {
        jump_fcontext(&ctx_, other.ctx_, other.vp_);
    }

    ALWAYS_INLINE void SwapOut()
    {
        jump_fcontext(&ctx_, GetTlsContext(), 0);
    }

    fcontext_t& GetTlsContext()
    {
        static thread_local fcontext_t tls_context;
        return tls_context;
    }

private:
    fcontext_t ctx_;
    fn_t fn_;
    intptr_t vp_;
    char* stack_ = nullptr;
    uint32_t stackSize_ = 0;
    uint32_t protectPage_ = 0;
};

} // namespace co
