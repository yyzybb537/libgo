#pragma once
#include "task.h"
#include "ts_queue.h"

namespace co {

struct ThreadLocalInfo;

// 协程执行器
//   管理一批协程的共享栈和调度, 非线程安全.
class Processer : public TSQueueHook
{
private:
    typedef TSQueue<Task> TaskList;

    uint32_t id_;
    std::atomic<uint32_t> task_count_{0};
    TaskList runnable_list_;

    static std::atomic<uint32_t> s_id_;

public:
    explicit Processer();

    void AddTaskRunnable(Task *tk);

    uint32_t Run(ThreadLocalInfo &info, uint32_t &done_count);

    void CoYield(ThreadLocalInfo &info);

    uint32_t GetTaskCount();
};

} //namespace co
