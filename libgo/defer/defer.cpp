#include "defer.h"
#include "../scheduler/scheduler.h"
#include "../cls/co_local_storage.h"

namespace co
{
    inline void*& GetLastDeferTls()
    {
        thread_local static void* p = nullptr;
        return p;
    }

    dismisser*& dismisser::GetLastDefer()
    {
        Task* tk = Processer::GetCurrentTask();
        if (tk) {
            CLS_REF(dismisser*) defer_cls = CLS(dismisser*, nullptr);
            return (dismisser*&)defer_cls;
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
