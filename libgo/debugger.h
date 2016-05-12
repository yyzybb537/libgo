#pragma once
#include "task.h"

namespace co
{

// libgo调试工具
class CoDebugger
{
public:
    static CoDebugger& getInstance();

    // 获取当前所有信息
    std::string GetAllInfo();

    // 当前协程总数量
    uint32_t TaskCount();

    // 当前协程ID, ID从1开始（不在协程中则返回0）
    uint64_t GetCurrentTaskID();

    // 当前协程切换的次数
    uint64_t GetCurrentTaskYieldCount();

    // 设置当前协程调试信息, 打印调试信息时将回显
    void SetCurrentTaskDebugInfo(std::string const& info);

    // 获取当前协程的调试信息, 返回的内容包括用户自定义的信息和协程ID
    const char* GetCurrentTaskDebugInfo();

    // 获取当前线程ID.(按执行调度器调度的顺序计)
    uint32_t GetCurrentThreadID();

    // 获取当前进程ID.
    uint32_t GetCurrentProcessID();

    // 获取当前计时器中的任务数量
    uint64_t GetTimerCount();

    // 获取当前sleep计时器中的任务数量
    uint64_t GetSleepTimerCount();

    // 获取当前所有协程的统计信息
    std::map<SourceLocation, uint32_t> GetTasksInfo();
    std::vector<std::map<SourceLocation, uint32_t>> GetTasksStateInfo();

private:
    CoDebugger() = default;
    ~CoDebugger() = default;
    CoDebugger(CoDebugger const&) = delete;
    CoDebugger& operator=(CoDebugger const&) = delete;

};

} //namespace co
