#include <Windows.h>
#include <algorithm>
#include <stdio.h>

namespace co
{
    
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
        static void*& GetTlsContext()
        {
            static thread_local void* native = nullptr;
            return native;
        }
    };

    static VOID WINAPI FiberFunc(LPVOID param)
    {
        std::function<void()> *fn = (std::function<void()>*)param;
        (*fn)();
    };

    class Context
    {
    public:
        Context(std::size_t stack_size, std::function<void()> const& fn)
            : fn_(fn), stack_size_(stack_size)
        {
            SIZE_T commit_size = 4 * 1024;
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

        ALWAYS_INLINE bool SwapIn()
        {
            SwitchToFiber(native_);
            return true;
        }

        ALWAYS_INLINE bool SwapOut()
        {
            SwitchToFiber(ContextScopedGuard::GetTlsContext());            
            return true;
        }

    private:
        std::function<void()> fn_;
        void *native_ = nullptr;
        uint32_t stack_size_ = 0;
    };

} //namespace co

