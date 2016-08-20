#pragma once
#include <deque>
#include <mutex>
#include <map>
#include <vector>
#include <libgo/spinlock.h>
#include <libgo/util.h>

#if defined(__GNUC__)
#include <cxxabi.h>
#include <stdlib.h>
#endif 

namespace co
{

struct ThreadLocalInfo;

// libgo调试工具
class CoDebugger
{
public:
    typedef atomic_t<uint64_t> count_t;
    typedef std::deque<std::pair<std::string, count_t>> object_counts_t;
    typedef std::deque<std::pair<std::string, uint64_t>> object_counts_result_t;

    /*
    * 调试用基类
    */
    template <typename Drived>
    struct DebuggerBase
    {
#if ENABLE_DEBUGGER
        DebuggerBase()
        {
            object_creator_.do_nothing();
            ++Count();
        }
        DebuggerBase(const DebuggerBase&)
        {
            object_creator_.do_nothing();
            ++Count();
        }
        ~DebuggerBase()
        {
            --Count();
        }

        static count_t& Count();

        struct object_creator
        {
            object_creator()
            {
                DebuggerBase<Drived>::Count();
            }
            inline void do_nothing() {}
        };

        static object_creator object_creator_;
#endif
    };

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

#if __linux__
    /// ------------ Linux -------------
    // 获取Fd统计信息
    std::string GetFdInfo();

    // 获取等待epoll的协程数量
    uint32_t GetEpollWaitCount();
#endif

    // 获取对象计数器统计信息
    object_counts_result_t GetDebuggerObjectCounts();

    // 线程局部对象
    ThreadLocalInfo& GetLocalInfo();

private:
    CoDebugger() = default;
    ~CoDebugger() = default;
    CoDebugger(CoDebugger const&) = delete;
    CoDebugger& operator=(CoDebugger const&) = delete;

    template <typename Drived>
    std::size_t GetDebuggerDrivedIndex()
    {
        static std::size_t s_index = s_debugger_drived_type_index_++;
        return s_index;
    }

private:
    LFLock object_counts_spinlock_;
    object_counts_t object_counts_;
    atomic_t<std::size_t> s_debugger_drived_type_index_{0};
};

template <typename T>
struct real_typename_helper {};

template <typename T>
std::string real_typename()
{
#if defined(__GNUC__)
    /// gcc.
    int s;
    char * realname = abi::__cxa_demangle(typeid(real_typename_helper<T>).name(), 0, 0, &s);
    std::string result(realname);
    free(realname);
#else
    std::string result(typeid(real_typename_helper<T>).name());
#endif 
    std::size_t start = result.find_first_of('<') + 1;
    std::size_t end = result.find_last_of('>');
    return result.substr(start, end - start);
}

#if ENABLE_DEBUGGER
template <typename Drived>
CoDebugger::count_t& CoDebugger::DebuggerBase<Drived>::Count()
{
    std::size_t index = CoDebugger::getInstance().GetDebuggerDrivedIndex<Drived>();
    auto &objs = CoDebugger::getInstance().object_counts_;
    if (objs.size() > index)
        return objs[index].second;

    std::unique_lock<LFLock> lock(CoDebugger::getInstance().object_counts_spinlock_);
    if (objs.size() > index)
        return objs[index].second;

    objs.resize(index + 1);
    objs[index].first = real_typename<Drived>();
    return objs[index].second;
}

template <typename Drived>
typename CoDebugger::DebuggerBase<Drived>::object_creator
    CoDebugger::DebuggerBase<Drived>::object_creator_;

#endif

} //namespace co
