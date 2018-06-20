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
    : public TSQueueHook
{
private:
    // 线程ID
    int id_;

    // 当前正在运行的协程
    Task* runningTask_{nullptr};
    LFLock test_;

    // 当前正在运行的协程本次调度开始的时间戳(Dispatch线程专用)
    uint64_t runningTick_ = 0;

    // 协程队列
    TSQueue<Task, false> runnableQueue_;
    TSQueue<Task, false> waitQueue_;
    TSQueue<Task, false> gcQueue_;

    TSQueue<Task> newQueue_;

    // WorkSteal
//    std::atomic_bool isStealing_{false};
//    std::mutex workStealMutex_;
//    std::condition_variable workStealCV_;

    // 等待的条件变量
    std::mutex cvMutex_;
    std::condition_variable cv_;
    std::atomic_bool waiting_{false};

    static int s_check_;

public:
    explicit Processer(int id);

    ALWAYS_INLINE int Id() { return id_; }

    void AddTaskRunnable(Task *tk);

    void Process();

    void CoYield();

    uint32_t GetTaskCount();

    static Processer* & GetCurrentProcesser();

    Task* GetCurrentTask();

    void NotifyCondition();

    std::size_t RunnableSize();

    ALWAYS_INLINE bool IsWaiting() { return waiting_.load(std::memory_order_acquire); }

private:
    void WaitCondition();

    void GC();

    void AddNewTasks();

    // TODO
    ALWAYS_INLINE void UpdateTick() {}
};

} //namespace co
