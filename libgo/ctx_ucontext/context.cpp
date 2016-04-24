#include <context.h>
#include <ucontext.h>
#include <scheduler.h>
#include "config.h"

namespace co
{
    ContextScopedGuard::ContextScopedGuard() {}
    ContextScopedGuard::~ContextScopedGuard() {}

    class Context::impl_t
    {
    public:
        ucontext_t ctx_;
        std::function<void()> fn_;
        char* stack_ = NULL;
        uint32_t stack_size_ = 0;

        ~impl_t()
        {
            if (stack_) {
                DebugPrint(dbg_task, "free stack. ptr=%p", stack_);
                free(stack_);
                stack_ = NULL;
            }
        }

        ucontext_t& GetTlsContext()
        {
            static thread_local ucontext_t tls_context;
            return tls_context;
        }
    };

    static void ucontext_func(std::function<void()>* pfn)
    {
        (*pfn)();
    }

    Context::Context(std::size_t stack_size, std::function<void()> const& fn)
        : impl_(new Context::impl_t), stack_size_(stack_size)
    {
        impl_->fn_ = fn;

        if (-1 == getcontext(&impl_->ctx_)) {
            ThrowError(eCoErrorCode::ec_makecontext_failed);
            return ;
        }

        impl_->stack_size_ = this->stack_size_;
        impl_->stack_ = (char*)valloc(impl_->stack_size_);
        DebugPrint(dbg_task, "valloc stack. size=%u ptr=%p",
                impl_->stack_size_, impl_->stack_);

        impl_->ctx_.uc_stack.ss_sp = impl_->stack_;
        impl_->ctx_.uc_stack.ss_size = impl_->stack_size_;
        impl_->ctx_.uc_link = NULL;
        makecontext(&impl_->ctx_, (void(*)(void))&ucontext_func, 1, &impl_->fn_);
    }

    bool Context::SwapIn()
    {
        return 0 == swapcontext(&impl_->GetTlsContext(), &impl_->ctx_);
    }

    bool Context::SwapOut()
    {
        return 0 == swapcontext(&impl_->ctx_, &impl_->GetTlsContext());
    }

} //namespace co

