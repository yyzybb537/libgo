#pragma once
#include <stddef.h>
#include <functional>
#include <exception>
#include <vector>
#include <list>
#include <set>
#include "config.h"
#include "context.h"
#include "ts_queue.h"
#include "timer.h"
#include <string.h>

namespace co
{

enum class TaskState
{
    init,
    runnable,
    io_block,       // write, writev, read, select, poll, ...
    sys_block,      // co_mutex, ...
    sleep,          // sleep, nanosleep, poll(NULL, 0, timeout)
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

// Network IO block所需的数据
struct IoWaitData
{
    std::atomic<uint32_t> io_block_id_{0}; // 每次io_block请求分配一个ID
    std::vector<FdStruct> wait_fds_;    // io_block等待的fd列表
    uint32_t wait_successful_ = 0;      // io_block成功等待到的fd数量(用于poll和select)
    LFLock io_block_lock_;              // 当等待的fd多余1个时, 用此锁sync添加到epoll和从epoll删除的操作, 以防在epoll中残留fd, 导致Task无法释放.
    int io_block_timeout_ = 0;
    CoTimerPtr io_block_timer_;
};

// 创建协程的源码文件位置
struct SourceLocation
{
    const char* file_ = nullptr;
    int lineno_ = 0;

    void Init(const char* file, int lineno)
    {
        file_ = file, lineno_ = lineno;
    }

    friend bool operator<(SourceLocation const& lhs, SourceLocation const& rhs)
    {
        if (!lhs.file_ && !rhs.file_) return false;
        if (!lhs.file_) return false;
        if (!rhs.file_) return true;

        int cmp = strcmp(lhs.file_, rhs.file_);
        if (cmp != 0) {
            return cmp == -1 ? true : false;
        }

        return lhs.lineno_ < rhs.lineno_;
    }

    std::string to_string() const
    {
        std::string s("{file:");
        if (file_) s += file_;
        s += ", line:";
        s += std::to_string(lineno_) + "}";
        return s;
    }
};

struct Task
    : public TSQueueHook
{
    uint64_t id_;
    TaskState state_ = TaskState::init;
    uint64_t yield_count_ = 0;
    Processer* proc_ = NULL;
    Context ctx_;
    TaskF fn_;
    std::string debug_info_;
    SourceLocation location_;
    std::exception_ptr eptr_;           // 保存exception的指针
    std::atomic<uint32_t> ref_count_{1};// 引用计数

    IoWaitData *io_wait_data_ = nullptr;// Network IO block所需的数据

    BlockObject* block_ = nullptr;      // sys_block等待的block对象
    uint32_t block_sequence_ = 0;       // sys_block等待序号(用于做超时校验)
    CoTimerPtr block_timer_;         // sys_block带超时等待所用的timer
	MininumTimeDurationType block_timeout_{ 0 }; // sys_block超时时间
    bool is_block_timeout_ = false;     // sys_block的等待是否超时

    int sleep_ms_ = 0;                  // 睡眠时间

    explicit Task(TaskF const& fn, std::size_t stack_size);
    ~Task();

    void InitLocation(const char* file, int lineno);
    void AddIntoProcesser(Processer *proc, char* shared_stack, uint32_t shared_stack_cap);

    bool SwapIn();
    bool SwapOut();

    void SetDebugInfo(std::string const& info);
    const char* DebugInfo();

    IoWaitData& GetIoWaitData();

    static uint64_t s_id;
    static std::atomic<uint64_t> s_task_count;

    // 引用计数
    void IncrementRef();
    void DecrementRef();
    static uint64_t GetTaskCount();

    // Task引用计数归0时不要立即释放, 以防epoll_wait取到残余数据时访问野指针.
    typedef TSQueue<Task> DeleteList;
    typedef std::shared_ptr<DeleteList> DeleteListPtr;

    static LFLock s_delete_lists_lock;
    static std::list<DeleteListPtr> s_delete_lists;

    static LFLock s_stat_lock;
    static std::set<Task*> s_stat_set;
    static std::map<SourceLocation, uint32_t> GetStatInfo();

    static void PopDeleteList(std::vector<SList<Task>> & output);
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
