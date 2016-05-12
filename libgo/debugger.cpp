#include "debugger.h"
#include "scheduler.h"

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
    s += "TaskCount: " + std::to_string(TaskCount());
    s += "\nCurrentTaskID: " + std::to_string(GetCurrentTaskID());
    s += "\nCurrentTaskInfo: " + std::string(GetCurrentTaskDebugInfo());
    s += "\nCurrentTaskYieldCount: " + std::to_string(GetCurrentTaskYieldCount());
    s += "\nCurrentThreadID: " + std::to_string(GetCurrentThreadID());
    s += "\nCurrentProcessID: " + std::to_string(GetCurrentProcessID());
    s += "\nTimerCount: " + std::to_string(GetTimerCount());
    s += "\nSleepTimerCount: " + std::to_string(GetSleepTimerCount());
    s += "\nTask Map: ------------------";
    auto vm = GetTasksStateInfo();
    for (std::size_t state = 0; state < vm.size(); ++state)
    {
        s += "\n  " + GetTaskStateName((TaskState)state) + " ->";
        for (auto &kv : vm[state])
        {
            s += "\n    " + std::to_string(kv.second) + " " + kv.first.to_string();
        }
    }
    s += "\n----------------------------";
    return s;
}
uint32_t CoDebugger::TaskCount()
{
    return g_Scheduler.TaskCount();
}
uint64_t CoDebugger::GetCurrentTaskID()
{
    return g_Scheduler.GetCurrentTaskID();
}
uint64_t CoDebugger::GetCurrentTaskYieldCount()
{
    return g_Scheduler.GetCurrentTaskYieldCount();
}
void CoDebugger::SetCurrentTaskDebugInfo(const std::string & info)
{
    g_Scheduler.SetCurrentTaskDebugInfo(info);
}
const char * CoDebugger::GetCurrentTaskDebugInfo()
{
    return g_Scheduler.GetCurrentTaskDebugInfo();
}
uint32_t CoDebugger::GetCurrentThreadID()
{
    return g_Scheduler.GetCurrentThreadID();
}
uint32_t CoDebugger::GetCurrentProcessID()
{
    return g_Scheduler.GetCurrentProcessID();
}
uint64_t CoDebugger::GetTimerCount()
{
    return g_Scheduler.timer_mgr_.Size();
}
uint64_t CoDebugger::GetSleepTimerCount()
{
    return g_Scheduler.sleep_wait_.timer_mgr_.Size();
}
std::map<SourceLocation, uint32_t> CoDebugger::GetTasksInfo()
{
    return Task::GetStatInfo();
}
std::vector<std::map<SourceLocation, uint32_t>> CoDebugger::GetTasksStateInfo()
{
    return Task::GetStateInfo();
}

} //namespace co
