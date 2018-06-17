#pragma once
#include "../common/config.h"
#include "../context/context.h"
#include "../common/ts_queue.h"
#include "../common/anys.h"

namespace co
{

enum class TaskState
{
    runnable,
    block,
    done,
};

//std::string GetTaskStateName(TaskState state);

typedef std::function<void()> TaskF;

struct TaskGroupKey {};
typedef Anys<TaskGroupKey> TaskAnys;

struct Processer;
struct Task
    : public TSQueueHook, public RefObject
{
    TaskState state_ = TaskState::runnable;
    uint64_t id_;
    Processer* proc_ = nullptr;
    Context ctx_;
    TaskF fn_;
    std::exception_ptr eptr_;           // 保存exception的指针
    TaskAnys anys_;

//    uint64_t id_;
//    bool is_affinity_ = false;  // 协程亲缘性
//    std::string debug_info_;
//    SourceLocation location_;
//
//    // Network IO block所需的数据
//    // shared_ptr不具有线程安全性, 只能在协程中和SchedulerSwitch中使用.
//    IoSentryPtr io_sentry_;     
//
//    BlockObject* block_ = nullptr;      // sys_block等待的block对象
//    uint32_t block_sequence_ = 0;       // sys_block等待序号(用于做超时校验)
//    CoTimerPtr block_timer_;         // sys_block带超时等待所用的timer
//	MininumTimeDurationType block_timeout_{ 0 }; // sys_block超时时间
//    bool is_block_timeout_ = false;     // sys_block的等待是否超时
//
//    int sleep_ms_ = 0;                  // 睡眠时间
//
//    // defer专用的cls存储
//    void *defer_cls_ = nullptr;
//
//    // cls变量表
//    CLSMap cls_map_;

//    explicit Task(TaskF const& fn, std::size_t stack_size,
//            const char* file, int lineno);

    Task(TaskF const& fn, std::size_t stack_size);
    ~Task();

//    void InitLocation(const char* file, int lineno);

    ALWAYS_INLINE bool SwapIn()
    {
        return ctx_.SwapIn();
    }
    ALWAYS_INLINE bool SwapOut()
    {
        return ctx_.SwapOut();
    }

    const char* DebugInfo();

private:
    void Run();

    Task(Task const&) = delete;
    Task(Task &&) = delete;
    Task& operator=(Task const&) = delete;
    Task& operator=(Task &&) = delete;
};

#define TaskInitPtr reinterpret_cast<Task*>(0x1)
#define TaskRefDefine(type, name) \
    ALWAYS_INLINE type& TaskRef ## name(Task *tk) \
    { \
        typedef type T; \
        static int idx = -1; \
        if (UNLIKELY(tk == TaskInitPtr)) { \
            if (idx == -1) \
                idx = TaskAnys::Register<T>(); \
            static T ignore{}; \
            return ignore; \
        } \
        return tk->anys_.get<T>(idx); \
    }
#define TaskRefInit(name) do { TaskRef ## name(TaskInitPtr); } while(0)

} //namespace co
