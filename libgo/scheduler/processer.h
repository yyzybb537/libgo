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

    enum class RunnableListType {
        normal,      // 普通的runnable队列, 允许被steal
        io_trigger,  // io触发后变回runnable状态的队列, 由于还有些epoll状态需要清理, 因此不允许被steal
        pri,         // 亲缘性协程队, 私有队列, 不能steal
        count,
    };

    Task* current_task_ = nullptr;
    TaskList runnable_list_[(int)RunnableListType::count];
    uint32_t id_;
    static atomic_t<uint32_t> s_id_;

    // 允许WorkSteal; io_block状态由于切换回来后要清理本线程的epoll上的监听信息，
    // 因此io_block状态回转到runnable的协程不可以切换到其他线程;
    TaskList io_runnable_list_;

public:
    explicit Processer();

    void AddTaskRunnable(Task *tk);

    uint32_t Run(uint32_t &done_count);

    void CoYield();

    uint32_t GetTaskCount();

    Task* GetCurrentTask();

    std::size_t StealHalf(Processer & other);

    std::size_t size();
};

} //namespace co
