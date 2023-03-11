#include "task.h"
#include "../common/config.h"
#include <iostream>
#include <string.h>
#include <string>
#include <algorithm>
#include "../debug/listener.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/ref.h"
#include "../routine_sync_libgo/libgo_switcher.h"

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
        SAFE_CALL_LISTENER(Listener::GetTaskListener(), onStart, this->id_);

        this->fn_();
        this->fn_ = TaskF(); //让协程function对象的析构也在协程中执行

        SAFE_CALL_LISTENER(Listener::GetTaskListener(), onCompleted, this->id_);
    };

    if (CoroutineOptions::getInstance().exception_handle == eCoExHandle::immedaitely_throw) {
        call_fn();
    } else {
        try {
            call_fn();
        } catch (...) {
            this->fn_ = TaskF();

            this->eptr_ = std::current_exception();
            DebugPrint(dbg_exception, "task(%s) catched exception.", DebugInfo());

            SAFE_CALL_LISTENER(Listener::GetTaskListener(), onException, this->id_, this->eptr_);
        }
    }

    SAFE_CALL_LISTENER(Listener::GetTaskListener(), onFinished, this->id_);

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
    extern_switcher_ = (void*)(new LibgoSwitcher);
}

Task::~Task()
{
//    printf("delete Task = %p, impl = %p, weak = %ld\n", this, this->impl_, (long)this->impl_->weak_);
    assert(!this->prev);
    assert(!this->next);
//    DebugPrint(dbg_task, "task(%s) destruct. this=%p", DebugInfo(), this);
    delete (LibgoSwitcher*)extern_switcher_;
    extern_switcher_ = nullptr;
}

const char* Task::DebugInfo()
{
    char& thiz = *reinterpret_cast<char*>(this);
    if (std::addressof(thiz) == nullptr) return "nil";

    return TaskDebugInfo(this);
}

} //namespace co
