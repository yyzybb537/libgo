#include "task.h"
#include "../common/config.h"
#include <iostream>
#include <string.h>
#include <string>
#include <algorithm>
#include "../debug/listener.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/ref.h"

#if USE_ROUTINE_SYNC
# include "../routine_sync_libgo/libgo_switcher.h"
#endif

namespace co
{

const char* GetTaskStateName(TaskState state)
{
    switch (state) {
    case TaskState::runnable:
        return "Runnable";
    case TaskState::block:
        return "Block";
    case TaskState::done:
        return "Done";
    default:
        return "Unkown";
    }
}

void Task::Run()
{
    auto call_fn = [this]() {
#if ENABLE_DEBUGGER
        if (Listener::GetTaskListener()) {
            Listener::GetTaskListener()->onStart(this->id_);
        }
#endif

        this->fn_();
        this->fn_ = TaskF(); //让协程function对象的析构也在协程中执行

#if ENABLE_DEBUGGER
        if (Listener::GetTaskListener()) {
            Listener::GetTaskListener()->onCompleted(this->id_);
        }
#endif
    };

    if (CoroutineOptions::getInstance().exception_handle == eCoExHandle::immedaitely_throw) {
        call_fn();
    } else {
        try {
            call_fn();
        } catch (...) {
            this->fn_ = TaskF();

            std::exception_ptr eptr = std::current_exception();
            DebugPrint(dbg_exception, "task(%s) catched exception.", DebugInfo());

#if ENABLE_DEBUGGER
            if (Listener::GetTaskListener()) {
                Listener::GetTaskListener()->onException(this->id_, eptr);
            }
#endif
        }
    }

#if ENABLE_DEBUGGER
    if (Listener::GetTaskListener()) {
        Listener::GetTaskListener()->onFinished(this->id_);
    }
#endif

    state_ = TaskState::done;
    Processer::StaticCoYield();
}

void FCONTEXT_CALL Task::StaticRun(intptr_t vp)
{
    Task* tk = (Task*)vp;
    tk->Run();
}

Task::Task(TaskF const& fn, std::size_t stack_size)
    : ctx_(&Task::StaticRun, (intptr_t)this, stack_size), fn_(fn)
{
//    DebugPrint(dbg_task, "task(%s) construct. this=%p", DebugInfo(), this);
#if USE_ROUTINE_SYNC
    extern_switcher_ = (void*)(new LibgoSwitcher);
#endif
}

Task::~Task()
{
//    printf("delete Task = %p, impl = %p, weak = %ld\n", this, this->impl_, (long)this->impl_->weak_);
    assert(!this->prev);
    assert(!this->next);
//    DebugPrint(dbg_task, "task(%s) destruct. this=%p", DebugInfo(), this);
#if USE_ROUTINE_SYNC
    delete (LibgoSwitcher*)extern_switcher_;
    extern_switcher_ = nullptr;
#endif
}

const char* Task::DebugInfo()
{
    if (reinterpret_cast<void*>(this) == nullptr) return "nil";

    return TaskDebugInfo(this);
}

} //namespace co
