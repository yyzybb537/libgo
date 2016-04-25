#include <context.h>
#include <scheduler.h>
#include <Windows.h>
#include <algorithm>

namespace co
{
    static void*& GetTlsContext()
    {
        static thread_local void* native = nullptr;
        return native;
    }

    struct ContextScopedGuard
    {
        ContextScopedGuard::ContextScopedGuard()
        {
            GetTlsContext() = ConvertThreadToFiber(nullptr);
        }
        ContextScopedGuard::~ContextScopedGuard()
        {
            ConvertFiberToThread();
            GetTlsContext() = nullptr;
        }
    };

    static VOID WINAPI FiberFunc(LPVOID param)
    {
        std::function<void()> *fn = (std::function<void()>*)param;
        (*fn)();
    };

    class Context
    {
        Context(std::size_t stack_size, std::function<void()> const& fn)
            : fn_(fn), stack_size_(stack_size)
        {
            SIZE_T commit_size = g_Scheduler.GetOptions().init_commit_stack_size;
            native_ = CreateFiberEx(commit_size,
                    std::max<std::size_t>(stack_size_, commit_size), FIBER_FLAG_FLOAT_SWITCH,
                    (LPFIBER_START_ROUTINE)FiberFunc, &fn_);
            if (!native_) {
                ThrowError(eCoErrorCode::ec_makecontext_failed);
                return ;
            }
        }
        ~Context()
        {
            if (native_) {
                DeleteFiber(native_);
                native_ = nullptr;
            }
        }

        inline bool SwapIn()
        {
            SwitchToFiber(native_);
            return true;
        }

        inline bool SwapOut()
        {
            SwitchToFiber(GetTlsContext());
            return true;
        }

    private:
        std::function<void()> fn_;
        void *native_ = nullptr;
        uint32_t stack_size_ = 0;
    };

} //namespace co

