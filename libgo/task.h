#pragma once
#include <libgo/config.h>
#include <libgo/context.h>
#include <libgo/ts_queue.h>
#include <libgo/timer.h>
#include <string.h>
#include <libgo/util.h>
#include "fd_context.h"

namespace co
{

enum class TaskState
{
    init,
    runnable,
    io_block,       // write, writev, read, select, poll, ...
    sys_block,      // co_mutex, ...
    sleep,          // sleep, nanosleep, poll(NULL, 0, timeout)
    done,
    fatal,
};

std::string GetTaskStateName(TaskState state);

typedef std::function<void()> TaskF;

class BlockObject;
class Processer;

struct Task
    : public TSQueueHook, public RefObject
{
    uint64_t id_;
    TaskState state_ = TaskState::init;
    uint64_t yield_count_ = 0;
    Processer* proc_ = NULL;
    Context ctx_;
    std::string debug_info_;
    TaskF fn_;
    SourceLocation location_;
    std::exception_ptr eptr_;           // 保存exception的指针

    // Network IO block所需的数据
    // shared_ptr不具有线程安全性, 只能在协程中和SchedulerSwitch中使用.
    IoSentryPtr io_sentry_;     

    BlockObject* block_ = nullptr;      // sys_block等待的block对象
    uint32_t block_sequence_ = 0;       // sys_block等待序号(用于做超时校验)
    CoTimerPtr block_timer_;         // sys_block带超时等待所用的timer
	MininumTimeDurationType block_timeout_{ 0 }; // sys_block超时时间
    bool is_block_timeout_ = false;     // sys_block的等待是否超时

    int sleep_ms_ = 0;                  // 睡眠时间

    explicit Task(TaskF const& fn, std::size_t stack_size,
            const char* file, int lineno);
    ~Task();

    void InitLocation(const char* file, int lineno);

    ALWAYS_INLINE bool SwapIn()
    {
        return ctx_.SwapIn();
    }
    ALWAYS_INLINE bool SwapOut()
    {
        return ctx_.SwapOut();
    }

    void SetDebugInfo(std::string const& info);
    const char* DebugInfo();

    void Task_CB();

    static uint64_t s_id;
    static atomic_t<uint64_t> s_task_count;

    static uint64_t GetTaskCount();

    static LFLock s_stat_lock;
    static std::set<Task*> s_stat_set;
    static std::map<SourceLocation, uint32_t> GetStatInfo();
    static std::vector<std::map<SourceLocation, uint32_t>> GetStateInfo();
};

} //namespace co
