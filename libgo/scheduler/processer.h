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
private:
    // 线程ID
    int id_;

    // 当前正在运行的协程
    Task* runningTask_{nullptr};
    Task* nextTask_{nullptr};

    // 当前线程开始运行的时间点
    int64_t runTick_ = 0;

    // 当前正在运行的协程本次调度开始的时间戳(Dispatch线程专用)
    uint64_t runningTick_ = 0;

    // 协程队列
    typedef TSQueue<Task, true> TaskQueue;
    TaskQueue runnableQueue_;
    TSQueue<Task, false> waitQueue_;
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

    void AddTaskRunnable(Task *tk);

    void Process();

    void CoYield();

    uint32_t GetTaskCount();

    static Processer* & GetCurrentProcesser();

    Task* GetCurrentTask();

    void NotifyCondition();

    std::size_t RunnableSize();

    ALWAYS_INLINE bool IsWaiting() { return waiting_; }

    bool IsTimeout();

private:
    void WaitCondition();

    void GC();

    bool AddNewTasks();

    void UpdateTick();

    int64_t NowMicrosecond();
};

} //namespace co
