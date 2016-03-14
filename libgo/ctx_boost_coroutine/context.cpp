#include <context.h>
#include <boost/coroutine/all.hpp>
#include <scheduler.h>

namespace co
{
    ContextScopedGuard::ContextScopedGuard() {}
    ContextScopedGuard::~ContextScopedGuard() {}

    template <typename traitsT>
    struct basic_shared_stack_allocator
    {
        typedef traitsT traits_type;

        char* stack_;
        uint32_t size_;

        basic_shared_stack_allocator(char* stack, uint32_t size)
            : stack_(stack), size_(size)
        {}

        void allocate(::boost::coroutines::stack_context & ctx,
                std::size_t size = traits_type::minimum_size())
        {
            BOOST_ASSERT( traits_type::minimum_size() <= size );
            BOOST_ASSERT( size <= size_ );
            ctx.size = size_;
            ctx.sp = stack_ + size_;
        }

        void deallocate(::boost::coroutines::stack_context & ctx)
        {
            // nothing need to do.
        }
    };
    typedef basic_shared_stack_allocator<::boost::coroutines::stack_traits> shared_stack_allocator;

    class Context::impl_t
    {
    public:
        ::boost::coroutines::symmetric_coroutine<void>::call_type ctx_;
        ::boost::coroutines::symmetric_coroutine<void>::yield_type *yield_ = nullptr;
        char* stack_ = NULL;
        uint32_t stack_size_ = 0;
        uint32_t stack_capacity_ = 0;
        char* shared_stack_;
        uint32_t shared_stack_cap_;
        std::function<void()> fn_;
    };

    Context::Context(std::size_t stack_size, std::function<void()> const& fn)
        : impl_(new Context::impl_t), stack_size_(stack_size)
    {
        impl_->fn_ = fn;

#if !defined(ENABLE_SHARED_STACK)
        decltype(impl_->ctx_) c(
            [=](::boost::coroutines::symmetric_coroutine<void>::yield_type& yield){
                impl_->yield_ = &yield;
                fn();
            }
            , boost::coroutines::attributes(std::max<std::size_t>(stack_size_, boost::coroutines::stack_traits::minimum_size()))
            );

        if (!c) {
            ThrowError(eCoErrorCode::ec_makecontext_failed);
            return ;
        }

        impl_->ctx_.swap(c);
#endif
    }

    bool Context::Init(char* shared_stack, uint32_t shared_stack_cap)
    {
#if defined(ENABLE_SHARED_STACK)
        impl_->shared_stack_ = shared_stack;
        impl_->shared_stack_cap_ = shared_stack_cap;
        decltype(impl_->ctx_) c(
            [=](::boost::coroutines::symmetric_coroutine<void>::yield_type& yield){
                impl_->yield_ = &yield;
                fn();
            }
            , boost::coroutines::attributes(shared_stack_cap), shared_stack_allocator(shared_stack, shared_stack_cap)
            );

        if (!c) return false;
        impl_->ctx_.swap(c);

        static const int default_base_size = 32;
        // save coroutine stack first 16 bytes.
        assert(!impl_->stack_);
        impl_->stack_size_ = default_base_size;
        impl_->stack_capacity_ = std::max<uint32_t>(default_base_size, g_Scheduler.GetOptions().init_stack_size);
        impl_->stack_ = (char*)malloc(impl_->stack_capacity_);
        memcpy(impl_->stack_, shared_stack + shared_stack_cap - impl_->stack_size_, impl_->stack_size_);
#endif
        return true;
    }

    bool Context::SwapIn()
    {
#if defined(ENABLE_SHARED_STACK)
        memcpy(impl_->shared_stack_ + impl_->shared_stack_cap_ - impl_->stack_size_, impl_->stack_, impl_->stack_size_);
#endif
        impl_->ctx_();
        return true;
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
        (*impl_->yield_)();
        return true;
    }

} //namespace co
