#pragma once
#include "task.h"
#include "ts_queue.h"

namespace co {

struct ThreadLocalInfo;

// 协程执行器
//   管理一批协程的共享栈和调度, 非线程安全.
class alignas(64) Processer
    : public TSQueueHook
{
private:
    typedef TSQueue<Task, false> TaskList;
    typedef TSQueue<Task> TaskSList;

    Task* current_task_ = nullptr;
    TaskList runnable_list_;
    TaskSList ts_runnable_list_;
    std::atomic<uint32_t> task_count_{0};
    uint32_t id_;
    static std::atomic<uint32_t> s_id_;

public:
    explicit Processer();

    void AddTaskRunnable(Task *tk);

    uint32_t Run(uint32_t &done_count);

    void CoYield();

    uint32_t GetTaskCount();

    Task* GetCurrentTask();
};

} //namespace co
