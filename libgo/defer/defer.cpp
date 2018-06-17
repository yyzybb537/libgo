#include "defer.h"
#include "scheduler.h"

namespace co
{
    inline void*& GetLastDeferTls()
    {
        thread_local static void* p = nullptr;
        return p;
    }

    dismisser*& dismisser::GetLastDefer()
    {
        Task* tk = Scheduler::getInstance().GetCurrentTask();
        if (tk) {
            return reinterpret_cast<dismisser*&>(tk->defer_cls_);
        }

        return reinterpret_cast<dismisser*&>(GetLastDeferTls());
    }

    void dismisser::SetLastDefer(dismisser* ptr)
    {
        GetLastDefer() = ptr;
    }

    void dismisser::ClearLastDefer(dismisser*)
    {
        GetLastDefer() = nullptr;
    }

    struct FakeDismisser : public dismisser
    {
        virtual bool dismiss() override { return false; }
    };

    dismisser& GetLastDefer()
    {
        dismisser* d = dismisser::GetLastDefer();
        if (d)
            return *d;

        static FakeDismisser nullD;
        return nullD;
    }

} // namespace co
