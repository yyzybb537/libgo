#include "debugger.h"
#include "../scheduler/ref.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"
#include "../task/task.h"
#include "../netio/unix/reactor.h"

namespace co
{

CoDebugger & CoDebugger::getInstance()
{
    static CoDebugger obj;
    return obj;
}
std::string CoDebugger::GetAllInfo()
{
    std::string s;
    s += P("==============================================");
    s += P("TaskCount: %d", TaskCount());
    s += P("CurrentTaskID: %lu", GetCurrentTaskID());
    s += P("CurrentTaskInfo: %s", GetCurrentTaskDebugInfo());
    s += P("CurrentTaskYieldCount: %lu", GetCurrentTaskYieldCount());
    s += P("CurrentThreadID: %d", GetCurrentThreadID());
#if defined(LIBGO_SYS_Unix)
    s += P("ReactorThreadNumber: %d", Reactor::GetReactorThreadCount());
#endif
    s += P("--------------------------------------------");
    s += P("Task Map:");
#if ENABLE_DEBUGGER
    auto mPtr = Task::SafeGetDbgMap();
    std::map<TaskState, std::map<SourceLocation, std::vector<Task*>>> locMap;
    for (auto & ptr : *mPtr)
    {
        Task* tk = (Task*)ptr;
        locMap[tk->state_][TaskRefLocation(tk)].push_back(tk);
    }

    for (auto & kkv : locMap) {
        s += P("  state = %s ->", GetTaskStateName(kkv.first));
        for (auto & kv : kkv.second)
        {
            s += P("    [%d] Loc {%s}", (int)kv.second.size(), kv.first.ToString().c_str());
            int i = 0;
            for (auto tk : kv.second) {
                s += P("     -> [%d] Task {%s}", i++, tk->DebugInfo());
            }
        }
    }
    mPtr.reset();
#else
    s += P("No data, please make libgo with 'cmake .. -DENABLE_DEBUGGER=ON'");
#endif
    s += P("--------------------------------------------");

    return s;
}
int CoDebugger::TaskCount()
{
#if ENABLE_DEBUGGER
    return Task::getCount();
#else
    return -1;
#endif
}
unsigned long CoDebugger::GetCurrentTaskID()
{
    return g_Scheduler.GetCurrentTaskID();
}
unsigned long CoDebugger::GetCurrentTaskYieldCount()
{
    return g_Scheduler.GetCurrentTaskYieldCount();
}
void CoDebugger::SetCurrentTaskDebugInfo(const std::string & info)
{
    g_Scheduler.SetCurrentTaskDebugInfo(info);
}
const char * CoDebugger::GetCurrentTaskDebugInfo()
{
    return Processer::GetCurrentTask()->DebugInfo();
}

} //namespace co
