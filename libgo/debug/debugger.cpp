#include <libgo/debugger.h>
#include <libgo/scheduler.h>

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
    s += "==============================================\n";
    s += "TaskCount: " + std::to_string(TaskCount());
    s += "\nCurrentTaskID: " + std::to_string(GetCurrentTaskID());
    s += "\nCurrentTaskInfo: " + std::string(GetCurrentTaskDebugInfo());
    s += "\nCurrentTaskYieldCount: " + std::to_string(GetCurrentTaskYieldCount());
    s += "\nCurrentThreadID: " + std::to_string(GetCurrentThreadID());
    s += "\nCurrentProcessID: " + std::to_string(GetCurrentProcessID());
    s += "\nTimerCount: " + std::to_string(GetTimerCount());
    s += "\nSleepTimerCount: " + std::to_string(GetSleepTimerCount());
    s += "\n--------------------------------------------";
    s += "\nTask Map:";
    auto vm = GetTasksStateInfo();
    for (std::size_t state = 0; state < vm.size(); ++state)
    {
        s += "\n  " + GetTaskStateName((TaskState)state) + " ->";
        for (auto &kv : vm[state])
        {
            s += "\n    " + std::to_string(kv.second) + " " + kv.first.to_string();
        }
    }
    s += "\n--------------------------------------------";

#if __linux__
    s += "\n" + GetFdInfo();
    s += "\nEpollWait:" + std::to_string(GetEpollWaitCount());
#endif

    s += "\n--------------------------------------------";
    s += "\nObject Counter:";
    auto objs = GetDebuggerObjectCounts();
    for (auto &kv : objs)
        s += "\nCount(" + kv.first + "): " + std::to_string((uint64_t)kv.second);
    s += "\n--------------------------------------------";

    s += "\n==============================================";
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
ThreadLocalInfo& CoDebugger::GetLocalInfo()
{
    return g_Scheduler.GetLocalInfo();
}

#if __linux__
/// ------------ Linux -------------
// 获取Fd统计信息
std::string CoDebugger::GetFdInfo()
{
    return FdManager::getInstance().GetDebugInfo();
}

// 获取等待epoll的协程数量
uint32_t CoDebugger::GetEpollWaitCount()
{
    return g_Scheduler.io_wait_.wait_io_sentries_.size();
}
#endif

CoDebugger::object_counts_result_t CoDebugger::GetDebuggerObjectCounts()
{
    object_counts_result_t result;
    std::unique_lock<LFLock> lock(CoDebugger::getInstance().object_counts_spinlock_);
    for (auto & elem : object_counts_)
        result.push_back(std::make_pair(elem.first, (uint64_t)elem.second));
    return result;
}


} //namespace co
