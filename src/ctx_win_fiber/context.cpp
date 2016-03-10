#include <context.h>
#include <scheduler.h>
#include <Windows.h>
#include <algorithm>

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

    Context::Context(std::size_t stack_size, std::function<void()> const& fn)
        : impl_(new Context::impl_t), stack_size_(stack_size)
    {
        impl_->fn_ = fn;
		SIZE_T commit_size = g_Scheduler.GetOptions().init_commit_stack_size;
		impl_->native_ = CreateFiberEx(commit_size,
				std::max<std::size_t>(stack_size_, commit_size), FIBER_FLAG_FLOAT_SWITCH,
                (LPFIBER_START_ROUTINE)FiberFunc, &impl_->fn_);
        if (!impl_->native_) {
            ThrowError(eCoErrorCode::ec_makecontext_failed);
            return ;
        }
    }

    bool Context::Init(char* shared_stack, uint32_t shared_stack_cap)
    {
        return true;
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

