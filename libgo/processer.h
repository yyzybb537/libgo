#pragma once
#include <libgo/config.h>
#include <libgo/task.h>
#include <libgo/ts_queue.h>

namespace co {

struct ThreadLocalInfo;

// 协程执行器
//   管理一批协程的共享栈和调度, 非线程安全.
class Processer
    : public TSQueueHook
{
private:
    typedef TSQueue<Task> TaskList;

    Task* current_task_ = nullptr;
    TaskList runnable_list_;
    uint32_t id_;
    static atomic_t<uint32_t> s_id_;

public:
    explicit Processer();

    void AddTaskRunnable(Task *tk);

    uint32_t Run(uint32_t &done_count);

    void CoYield();

    uint32_t GetTaskCount();

    Task* GetCurrentTask();

    std::size_t StealHalf(Processer & other);
};

} //namespace co
