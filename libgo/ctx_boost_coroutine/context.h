#include <boost/coroutine/all.hpp>

namespace co
{
    struct ContextScopedGuard {};

    template< typename traitsT >
        struct my_standard_stack_allocator
        {
            typedef traitsT traits_type;

            uint32_t & protect_page_;

            explicit my_standard_stack_allocator(uint32_t & protect_page)
                : protect_page_(protect_page)
            {
            }

            void allocate(boost::coroutines::stack_context & ctx, std::size_t size = traits_type::minimum_size())
            {
                BOOST_ASSERT(traits_type::minimum_size() <= size);
                BOOST_ASSERT(traits_type::is_unbounded() || (traits_type::maximum_size() >= size));

                void * limit = StackAllocator::get_malloc_fn()( size);
                if ( ! limit) throw std::bad_alloc();

                ctx.size = size;
                ctx.sp = static_cast<char *>(limit) + ctx.size;
#if __linux__
                uint32_t protect_page = StackAllocator::get_protect_stack_page();
                if (protect_page)
                    if (StackAllocator::protect_stack(limit, size, protect_page))
                        protect_page_ = protect_page;
#endif

#if defined(BOOST_USE_VALGRIND)
                ctx.valgrind_stack_id = VALGRIND_STACK_REGISTER(ctx.sp, limit);
#endif
            }

            void deallocate(boost::coroutines::stack_context & ctx)
            {
                BOOST_ASSERT(ctx.sp);
                BOOST_ASSERT(traits_type::minimum_size() <= ctx.size);
                BOOST_ASSERT(traits_type::is_unbounded() || (traits_type::maximum_size() >= ctx.size));

#if defined(BOOST_USE_VALGRIND)
                VALGRIND_STACK_DEREGISTER( ctx.valgrind_stack_id);
#endif

                void * limit = static_cast<char *>(ctx.sp) - ctx.size;
#if __linux__
                if (protect_page_)
                    StackAllocator::unprotect_stack(limit, protect_page_);
#endif
                StackAllocator::get_free_fn()(limit);
            }
        };

    typedef my_standard_stack_allocator<boost::coroutines::stack_traits>  my_stack_allocator;

    class Context
    {
        public:
            Context(std::size_t stack_size, std::function<void()> const& fn)
                : protect_page_(0), ctx_([=](::boost::coroutines::symmetric_coroutine<void>::yield_type& yield){
                        this->yield_ = &yield;
                        fn();
                        },
                        boost::coroutines::attributes(std::max<std::size_t>(
                                stack_size, boost::coroutines::stack_traits::minimum_size())),
                        my_stack_allocator(protect_page_)),
                yield_(nullptr)
                {
                    if (!ctx_) {
                        ThrowError(eCoErrorCode::ec_makecontext_failed);
                        return ;
                    }
                }

            ALWAYS_INLINE bool SwapIn()
            {
                ctx_();
                return true;
            }

            ALWAYS_INLINE bool SwapOut()
            {
                (*yield_)();
                return true;
            }

        private:
            uint32_t protect_page_;
            ::boost::coroutines::symmetric_coroutine<void>::call_type ctx_;
            ::boost::coroutines::symmetric_coroutine<void>::yield_type *yield_ = nullptr;
    };

} //namespace co
