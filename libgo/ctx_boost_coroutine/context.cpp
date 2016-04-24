#include <context.h>
#include <boost/coroutine/all.hpp>
#include <scheduler.h>

namespace co
{
    ContextScopedGuard::ContextScopedGuard() {}
    ContextScopedGuard::~ContextScopedGuard() {}

    class Context::impl_t
    {
    public:
        ::boost::coroutines::symmetric_coroutine<void>::call_type ctx_;
        ::boost::coroutines::symmetric_coroutine<void>::yield_type *yield_ = nullptr;
        std::function<void()> fn_;
    };

    Context::Context(std::size_t stack_size, std::function<void()> const& fn)
        : impl_(new Context::impl_t), stack_size_(stack_size)
    {
        impl_->fn_ = fn;

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
    }

    bool Context::SwapIn()
    {
        impl_->ctx_();
        return true;
    }

    bool Context::SwapOut()
    {
        (*impl_->yield_)();
        return true;
    }

} //namespace co
