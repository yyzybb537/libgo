#pragma once
#include <stddef.h>
#include <ucontext.h>
#include <functional>
#include <exception>
#include <vector>
#include <list>
#include "ts_queue.h"
#include "timer.h"

namespace co
{

enum class TaskState
{
    init,
    runnable,
    io_block,       // write, writev, read, select, poll, ...
    sys_block,      // co_mutex, ...
    user_block,     // user switch it.
    sleep,          // sleep nanosleep poll(NULL, 0, timeout)
    done,
    fatal,
};

typedef std::function<void()> TaskF;

struct FdStruct;
struct Task;

struct EpollPtr
{
    FdStruct* fdst = NULL;
    Task* tk = NULL;
    uint32_t io_block_id = 0;
    uint32_t revent = 0;    // 结果event
};

struct FdStruct
{
    int fd;
    uint32_t event;     // epoll event flags.
    EpollPtr epoll_ptr; // 传递入epoll的指针

    FdStruct() : fd(-1), event(0) {
        epoll_ptr.fdst = this;
    }
};

class BlockObject;
class Processer;
struct Task
    : public TSQueueHook
{
    uint64_t id_;
    TaskState state_ = TaskState::init;
    uint64_t yield_count_ = 0;
    ucontext_t ctx_;
    TaskF fn_;
    Processer* proc_ = NULL;
    char* stack_ = NULL;
    uint32_t stack_size_ = 0;
    uint32_t stack_capacity_ = 0;
    std::string debug_info_;
    std::exception_ptr eptr_;           // 保存exception的指针
    std::atomic<uint32_t> ref_count_{1};// 引用计数

    std::atomic<uint32_t> io_block_id_; // 每次io_block请求分配一个ID
    std::vector<FdStruct> wait_fds_;    // io_block等待的fd列表
    uint32_t wait_successful_ = 0;      // io_block成功等待到的fd数量(用于poll和select)
    LFLock io_block_lock_;              // 当等待的fd多余1个时, 用此锁sync添加到epoll和从epoll删除的操作, 以防在epoll中残留fd, 导致Task无法释放.
    int io_block_timeout_ = 0;
    CoTimerPtr io_block_timer_;

    int64_t user_wait_type_ = 0;        // user_block等待的类型
    uint64_t user_wait_id_ = 0;         // user_block等待的id
    BlockObject* block_ = NULL;         // sys_block等待的block对象

    int sleep_ms_ = 0;                  // 睡眠时间

    explicit Task(TaskF const& fn);
    ~Task();

    void AddIntoProcesser(Processer *proc, char* shared_stack, uint32_t shared_stack_cap);

    void SetDebugInfo(std::string const& info);
    const char* DebugInfo();

    static uint64_t s_id;
    static std::atomic<uint64_t> s_task_count;

    void IncrementRef();
    void DecrementRef();
    static uint64_t GetTaskCount();

    // Task引用计数归0时不要立即释放, 以防epoll_wait取到残余数据时访问野指针.
    typedef std::list<Task*> DeleteList;
    static DeleteList s_delete_list;
    static LFLock s_delete_list_lock;

    static void SwapDeleteList(DeleteList &output);
    static std::size_t GetDeletedTaskCount();
};

template <typename T = Task>
class RefGuard
{
public:
    explicit RefGuard(T* tk) : tk_(tk)
    {
        tk_->IncrementRef();
    }
    explicit RefGuard(T& tk) : tk_(&tk)
    {
        tk_->IncrementRef();
    }
    ~RefGuard()
    {
        tk_->DecrementRef();
    }

    RefGuard(RefGuard const&) = delete;
    RefGuard& operator=(RefGuard const&) = delete;

private:
    T *tk_;
};


} //namespace co
