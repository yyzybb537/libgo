#include <boost/context/all.hpp>

namespace co
{
    struct ContextScopedGuard {};

    class Context
    {
    public:
        Context(std::size_t stack_size, std::function<void()> const& fn)
            : fn_(fn), stack_size_(stack_size)
        {
            BOOST_ASSERT(boost::context::stack_traits::minimum_size() <= stack_size_);
            BOOST_ASSERT(boost::context::stack_traits::is_unbounded() || (boost::context::stack_traits::maximum_size()>= stack_size_));

            stack_ = (char*)StackAllocator::get_malloc_fn()(stack_size_);
            DebugPrint(dbg_task, "valloc stack. size=%u ptr=%p",
                    stack_size_, stack_);

            void* sp = (void*)((char*)stack_ + stack_size_);
            ctx_ = boost::context::make_fcontext(sp, stack_size_, &fcontext_func);

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
                stack_ = nullptr;
            }
        }

        ALWAYS_INLINE bool SwapIn()
        {
            boost::context::jump_fcontext(&GetTlsContext(), ctx_, (intptr_t)this, true);
            return true;
        }

        ALWAYS_INLINE bool SwapOut()
        {
            boost::context::jump_fcontext(&ctx_, GetTlsContext(), (intptr_t)this, true);
            return true;
        }

        boost::context::fcontext_t& GetTlsContext()
        {
            static thread_local boost::context::fcontext_t tls_context;
            return tls_context;
        }

        static void fcontext_func(intptr_t arg)
        {
            Context* self = (Context*)arg;
            self->fn_();
        }

    private:
        boost::context::fcontext_t ctx_;
        std::function<void()> fn_;
        char* stack_ = nullptr;
        uint32_t stack_size_ = 0;
        uint32_t protect_page_ = 0;
    };

} //namespace co

