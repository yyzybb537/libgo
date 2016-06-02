#include <ucontext.h>

namespace co
{
    struct ContextScopedGuard {};

    class Context
    {
    public:
        Context(std::size_t stack_size, std::function<void()> const& fn)
            : fn_(fn), stack_size_(stack_size)
        {
            if (-1 == getcontext(&ctx_)) {
                ThrowError(eCoErrorCode::ec_makecontext_failed);
                return ;
            }

            stack_ = (char*)StackAllocator::get_malloc_fn()(stack_size_);
            DebugPrint(dbg_task, "valloc stack. size=%u ptr=%p",
                    stack_size_, stack_);

            ctx_.uc_stack.ss_sp = stack_;
            ctx_.uc_stack.ss_size = stack_size_;
            ctx_.uc_link = NULL;

            makecontext(&ctx_, (void(*)(void))&ucontext_func, 1, &fn_);

            uint32_t protect_page = StackAllocator::get_protect_stack_page();
            if (protect_page)
                if (StackAllocator::protect_stack(stack_, stack_size_, protect_page))
                    protect_page_ = protect_page;
        }
        ~Context()
        {
            if (stack_) {
                DebugPrint(dbg_task, "free stack. ptr=%p", stack_);
                if (protect_page_)
                    StackAllocator::unprotect_stack(stack_, protect_page_);
                StackAllocator::get_free_fn()(stack_);
                stack_ = NULL;
            }
        }

        ALWAYS_INLINE bool SwapIn()
        {
            return 0 == swapcontext(&GetTlsContext(), &ctx_);
        }

        ALWAYS_INLINE bool SwapOut()
        {
            return 0 == swapcontext(&ctx_, &GetTlsContext());
        }

        ucontext_t& GetTlsContext()
        {
            static thread_local ucontext_t tls_context;
            return tls_context;
        }

        static void ucontext_func(std::function<void()>* pfn)
        {
            (*pfn)();
        }

    private:
        ucontext_t ctx_;
        std::function<void()> fn_;
        char* stack_ = nullptr;
        uint32_t stack_size_ = 0;
        uint32_t protect_page_ = 0;
    };

} //namespace co

