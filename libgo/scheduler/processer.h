#pragma once
#include "../common/config.h"
#include "../task/task.h"
#include "../common/ts_queue.h"
#include <condition_variable>
#include <mutex>
#include <atomic>
#include "../timer/timer.h"

namespace co {

// 协程执行器
// 对应一个线程, 负责本线程的协程调度, 非线程安全.
class Processer
{
    friend class Scheduler;
private:
    // 线程ID
    int id_;

    // 激活态
    // 非激活的P仅仅是不能接受新的协程加入, 仍然可以强行AddTask并正常处理.
    volatile bool active_ = true;

    // 当前正在运行的协程
    Task* runningTask_{nullptr};
    Task* nextTask_{nullptr};

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
    std::mutex cvMutex_;
    std::condition_variable cv_;
    std::atomic_bool waiting_{false};

    static int s_check_;

public:
    explicit Processer(int id);

    ALWAYS_INLINE int Id() { return id_; }

    // 新创建、阻塞后触发的协程add进来
    void AddTaskRunnable(Task *tk);

    // 偷来的协程add进来
    void AddTaskRunnable(SList<Task> && slist);

    void Process();

    void CoYield();

    static Processer* & GetCurrentProcesser();

    Task* GetCurrentTask();

    void NotifyCondition();

    // 待执行的协程数量
    // 暂兼用于负载指数
    std::size_t RunnableSize();

    // 是否处于等待状态(无runnable协程)
    // 调度线程会尽量分配协程过来
    ALWAYS_INLINE bool IsWaiting() { return waiting_; }

    // 单个协程执行时长超过预设值, 则判定为阻塞状态
    // 阻塞状态不再加入新的协程, 并由调度线程steal走所有协程(正在执行的除外)
    bool IsBlocking();

    // 偷协程
    SList<Task> Steal(std::size_t n);

    // 挂起当前协程
    static uint64_t Suspend();

    // 唤醒协程
    static bool Wakeup(Task* tk, uint64_t id);

private:
    void WaitCondition();

    void GC();

    bool AddNewTasks();

    // 调度线程打标记, 用于检测阻塞
    void Mark();

    int64_t NowMicrosecond();

    uint64_t SuspendBySelf(Task* tk);

    bool WakeupBySelf(Task* tk, uint64_t id);
};

} //namespace co
