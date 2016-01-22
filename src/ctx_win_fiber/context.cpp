#include <context.h>
#include <scheduler.h>
#include <Windows.h>

namespace co
{
    static void*& GetTlsContext()
    {
        static co_thread_local void* native = nullptr;
        return native;
    }

    ContextScopedGuard::ContextScopedGuard()
    {
        GetTlsContext() = ConvertThreadToFiber(nullptr);
    }
    ContextScopedGuard::~ContextScopedGuard()
    {
        ConvertFiberToThread();
        GetTlsContext() = nullptr;
    }

    class Context::impl_t
    {
    public:
        std::function<void()> fn_;
        void *native_ = nullptr;

        ~impl_t()
        {
            if (native_) {
                DeleteFiber(native_);
                native_ = nullptr;
            }
        }
    };

    static VOID WINAPI FiberFunc(LPVOID param)
    {
        std::function<void()> *fn = (std::function<void()>*)param;
        (*fn)();
    };

    Context::Context()
        : impl_(new Context::impl_t)
    {}

    bool Context::Init(std::function<void()> const& fn, char* shared_stack, uint32_t shared_stack_cap)
    {
        impl_->fn_ = fn;
        impl_->native_ = CreateFiberEx(g_Scheduler.GetOptions().init_commit_stack_size,
                g_Scheduler.GetOptions().stack_size, FIBER_FLAG_FLOAT_SWITCH,
                (LPFIBER_START_ROUTINE)FiberFunc, &impl_->fn_);
        return !!impl_->native_;
    }

    bool Context::SwapIn()
    {
        SwitchToFiber(impl_->native_);
        return true;
    }

    bool Context::SwapOut()
    {
        SwitchToFiber(GetTlsContext());
        return true;
    }

} //namespace co

