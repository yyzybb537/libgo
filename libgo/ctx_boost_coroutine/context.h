#include <boost/coroutine/all.hpp>

namespace co
{
    struct ContextScopedGuard {};

    class Context
    {
    public:
        Context(std::size_t stack_size, std::function<void()> const& fn)
            : ctx_([=](::boost::coroutines::symmetric_coroutine<void>::yield_type& yield){
                        this->yield_ = &yield;
                        fn();
                    },
                    boost::coroutines::attributes(std::max<std::size_t>(
                            stack_size, boost::coroutines::stack_traits::minimum_size()))),
            yield_(nullptr)
        {
            if (!ctx_) {
                ThrowError(eCoErrorCode::ec_makecontext_failed);
                return ;
            }
        }
        
        inline bool SwapIn()
        {
            ctx_();
            return true;
        }

        inline bool SwapOut()
        {
            (*yield_)();
            return true;
        }

    private:
        ::boost::coroutines::symmetric_coroutine<void>::call_type ctx_;
        ::boost::coroutines::symmetric_coroutine<void>::yield_type *yield_ = nullptr;
    };

} //namespace co
