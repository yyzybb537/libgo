#pragma once
#include "../common/config.h"
#include "../common/clock.h"
#include "../task/task.h"
#include "../common/ts_queue.h"

#if ENABLE_DEBUGGER
#include "../debug/listener.h"
#endif
#include <condition_variable>
#include <mutex>
#include <atomic>

namespace co {

class Scheduler;

// 协程执行器
// 对应一个线程, 负责本线程的协程调度, 非线程安全.
class Processer
{
    friend class Scheduler;

private:
    Scheduler * scheduler_;

    // 线程ID
    int id_;

    // 激活态
    // 非激活的P仅仅是不能接受新的协程加入, 仍然可以强行AddTask并正常处理.
    volatile bool active_ = true;

    // 当前正在运行的协程
    Task* runningTask_{nullptr};
    Task* nextTask_{nullptr};

    // 每轮调度只加有限次数新协程, 防止新协程创建新协程产生死循环
    int addNewQuota_ = 0;

    // 当前正在运行的协程本次调度开始的时间戳(Dispatch线程专用)
    volatile int64_t markTick_ = 0;
    volatile uint64_t markSwitch_ = 0;

    // 协程调度次数
    volatile uint64_t switchCount_ = 0;

    // 协程队列
    typedef TSQueue<Task, true> TaskQueue;
    TaskQueue runnableQueue_;
    TaskQueue waitQueue_;
    TSQueue<Task, false> gcQueue_;

    TaskQueue newQueue_;

    // 等待的条件变量
    std::condition_variable_any cv_;
    std::atomic_bool waiting_{false};
    bool notified_ = false;

    static int s_check_;

public:
    ALWAYS_INLINE int Id() { return id_; }

    static Processer* & GetCurrentProcesser();

    static Scheduler* GetCurrentScheduler();

    inline Scheduler* GetScheduler() { return scheduler_; }

    // 获取当前正在执行的协程
    static Task* GetCurrentTask();

    // 是否在协程中
    static bool IsCoroutine();

    // 协程切出
    ALWAYS_INLINE static void StaticCoYield();

    // 挂起标识
    struct SuspendEntry {
        WeakPtr<Task> tk_;
        uint64_t id_;

        explicit operator bool() const { return !!tk_; }

        friend bool operator==(SuspendEntry const& lhs, SuspendEntry const& rhs) {
            return lhs.tk_ == rhs.tk_ && lhs.id_ == rhs.id_;
        }

        friend bool operator<(SuspendEntry const& lhs, SuspendEntry const& rhs) {
            if (lhs.id_ == rhs.id_)
                return lhs.tk_ < rhs.tk_;
            return lhs.id_ < rhs.id_;
        }

        bool IsExpire() const {
            return Processer::IsExpire(*this);
        }
    };

    // 挂起当前协程
    static SuspendEntry Suspend();

    // 挂起当前协程, 并在指定时间后自动唤醒
    static SuspendEntry Suspend(FastSteadyClock::duration dur);
    static SuspendEntry Suspend(FastSteadyClock::time_point timepoint);

    // 唤醒协程
    static bool Wakeup(SuspendEntry const& entry, std::function<void()> const& functor = NULL);

    // 测试一个SuspendEntry是否还可能有效
    static bool IsExpire(SuspendEntry const& entry);

    /// --------------------------------------
    // for friend class Scheduler
private:
    explicit Processer(Scheduler * scheduler, int id);

    // 待执行的协程数量
    // 暂兼用于负载指数
    std::size_t RunnableSize();

    ALWAYS_INLINE void CoYield();

    // 新创建、阻塞后触发的协程add进来
    void AddTask(Task *tk);

    // 调度
    void Process();

    // 偷来的协程add进来
    void AddTask(SList<Task> && slist);

    void NotifyCondition();

    // 是否处于等待状态(无runnable协程)
    // 调度线程会尽量分配协程过来
    ALWAYS_INLINE bool IsWaiting() { return waiting_; }

    // 单个协程执行时长超过预设值, 则判定为阻塞状态
    // 阻塞状态不再加入新的协程, 并由调度线程steal走所有协程(正在执行的除外)
    bool IsBlocking();

    // 偷协程
    SList<Task> Steal(std::size_t n);
    /// --------------------------------------

private:
    void WaitCondition();

    void GC();

    bool AddNewTasks();

    // 调度线程打标记, 用于检测阻塞
    void Mark();

    int64_t NowMicrosecond();

    SuspendEntry SuspendBySelf(Task* tk);

    bool WakeupBySelf(IncursivePtr<Task> const& tkPtr, uint64_t id, std::function<void()> const& functor);
};

ALWAYS_INLINE void Processer::StaticCoYield()
{
    auto proc = GetCurrentProcesser();
    if (proc) proc->CoYield();
}

ALWAYS_INLINE void Processer::CoYield()
{
    Task *tk = GetCurrentTask();
    assert(tk);

    ++ tk->yieldCount_;

#if ENABLE_DEBUGGER
    DebugPrint(dbg_yield, "yield task(%s) state = %s", tk->DebugInfo(), GetTaskStateName(tk->state_));
    if (Listener::GetTaskListener())
        Listener::GetTaskListener()->onSwapOut(tk->id_);
#endif

    tk->SwapOut();
}


} //namespace co
