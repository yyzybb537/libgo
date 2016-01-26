#include <context.h>
#include <ucontext.h>
#include <scheduler.h>

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

#if defined(ENABLE_SHARED_STACK)
        uint32_t stack_capacity_ = 0;
        char* shared_stack_;
        uint32_t shared_stack_cap_;
#endif

        ~impl_t()
        {
            if (stack_) {
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

    Context::Context(std::size_t stack_size)
        : impl_(new Context::impl_t), stack_size_(stack_size)
    {}

    bool Context::Init(std::function<void()> const& fn, char* shared_stack, uint32_t shared_stack_cap)
    {
        if (-1 == getcontext(&impl_->ctx_))
            return false;

        impl_->fn_ = fn;

#if defined(ENABLE_SHARED_STACK)
        impl_->shared_stack_ = shared_stack;
        impl_->shared_stack_cap_ = shared_stack_cap;

        impl_->ctx_.uc_stack.ss_sp = shared_stack;
        impl_->ctx_.uc_stack.ss_size = shared_stack_cap;
        impl_->ctx_.uc_link = NULL;
        makecontext(&impl_->ctx_, (void(*)(void))&ucontext_func, 1, &impl_->fn_);

        // save coroutine stack first 16 bytes.
        assert(!impl_->stack_);
        impl_->stack_size_ = 16;
        impl_->stack_capacity_ = std::max<uint32_t>(16, g_Scheduler.GetOptions().init_commit_stack_size);
        impl_->stack_ = (char*)malloc(impl_->stack_capacity_);
        memcpy(impl_->stack_, shared_stack + shared_stack_cap - impl_->stack_size_, impl_->stack_size_);
#else
        impl_->stack_size_ = this->stack_size_;
        impl_->stack_ = (char*)valloc(impl_->stack_size_);

        impl_->ctx_.uc_stack.ss_sp = impl_->stack_;
        impl_->ctx_.uc_stack.ss_size = impl_->stack_size_;
        impl_->ctx_.uc_link = NULL;
        makecontext(&impl_->ctx_, (void(*)(void))&ucontext_func, 1, &impl_->fn_);
#endif

        return true;
    }

    bool Context::SwapIn()
    {
#if defined(ENABLE_SHARED_STACK)
        memcpy(impl_->shared_stack_ + impl_->shared_stack_cap_ - impl_->stack_size_, impl_->stack_, impl_->stack_size_);
#endif
        return 0 == swapcontext(&impl_->GetTlsContext(), &impl_->ctx_);
    }

    bool Context::SwapOut()
    {
#if defined(ENABLE_SHARED_STACK)
        char dummy = 0;
        char *top = impl_->shared_stack_ + impl_->shared_stack_cap_;
        uint32_t current_stack_size = top - &dummy;
        assert(current_stack_size <= impl_->shared_stack_cap_);
        if (impl_->stack_capacity_ < current_stack_size) {
            impl_->stack_ = (char*)realloc(impl_->stack_, current_stack_size);
            impl_->stack_capacity_ = current_stack_size;
        }
        impl_->stack_size_ = current_stack_size;
        memcpy(impl_->stack_, &dummy, impl_->stack_size_);
#endif
        return 0 == swapcontext(&impl_->ctx_, &impl_->GetTlsContext());
    }

} //namespace co

