#include <context.h>
#include <boost/coroutine/all.hpp>
#include <scheduler.h>

namespace co
{
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
            return stack_;
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
    };

    Context::Context()
        : impl_(new Context::impl_t)
    { }

    bool Context::Init(std::function<void()> const& fn, char* stack, uint32_t stack_size)
    {
        decltype(impl_->ctx_) c(
            [=](::boost::coroutines::symmetric_coroutine<void>::yield_type& yield){
                impl_->yield_ = &yield;
                fn();
            }
#if !defined(BOOST_USE_SEGMENTED_STACKS)
            , boost::coroutines::attributes(stack_size), shared_stack_allocator(stack, stack_size)
#endif
            );
        if (!c) return false;
        impl_->ctx_.swap(c);
        return true;
    }

    bool Context::SwapIn()
    {
        try {
            impl_->ctx_();
            return true;
        } catch(...) {
            return false;
        }
    }

    bool Context::SwapOut()
    {
        try {
            (*impl_->yield_)();
            return true;
        } catch(...) {
            return false;
        }
    }

} //namespace co
