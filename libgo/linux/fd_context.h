#pragma once
#include <libgo/config.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <mutex>
#include <list>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <deque>
#include <poll.h>
#include <libgo/ts_queue.h>
#include <libgo/spinlock.h>
#include <libgo/util.h>
#include <libgo/timer.h>
#include <libgo/debugger.h>

namespace co {

struct Task;
typedef std::weak_ptr<Task> TaskWeakPtr;
typedef std::shared_ptr<Task> TaskPtr;

class FileDescriptorCtx;
typedef std::shared_ptr<FileDescriptorCtx> FdCtxPtr;
typedef std::weak_ptr<FileDescriptorCtx> FdCtxWeakPtr;

struct IoSentry
    : public RefObject, public TSQueueHook, public CoDebugger::DebuggerBase<IoSentry>
{
    enum task_io_state
    {
        pending,
        triggered,
    };

    atomic_t<long> io_state_;
    std::vector<pollfd> watch_fds_; //会被多线程并行访问, add_into_reactor后长度不能变
    CoTimerPtr timer_;
    TaskPtr task_ptr_;

    explicit IoSentry(Task* tk, pollfd *fds, nfds_t nfds);
    ~IoSentry();

    // return: cas pending to triggered
    bool switch_state_to_triggered();

    void triggered_by_add(int fd, int revents);
};
typedef std::weak_ptr<IoSentry> IoSentryWeakPtr;
typedef std::shared_ptr<IoSentry> IoSentryPtr;
typedef std::set<IoSentryPtr> TriggerSet;

enum class add_into_reactor_result
{
    failed, 
    progress,
    complete,
};

class FileDescriptorCtx
    : public std::enable_shared_from_this<FileDescriptorCtx>
{
public:
    typedef std::map<Task*, IoSentryWeakPtr> TaskWSet;

    explicit FileDescriptorCtx(int fd);
    ~FileDescriptorCtx();

    bool re_initialize();

    bool is_initialize();
    bool is_socket();
    bool is_epoll();
    bool closed();
    int close(bool call_syscall);

    void set_user_nonblock(bool b);
    bool user_nonblock();

    void set_sys_nonblock(bool b);
    bool sys_nonblock();

    void set_is_epoll();

    void set_et_mode();
    bool is_et_mode();

    ALWAYS_INLINE bool readable();
    ALWAYS_INLINE bool writable();

    ALWAYS_INLINE void set_readable(bool b);
    ALWAYS_INLINE void set_writable(bool b);

    // @type: SO_RCVTIMEO SO_SNDTIMEO
    void set_time_o(int type, timeval const& tv);
    void get_time_o(int type, timeval *tv);

    // multiple threads called in coroutines
    add_into_reactor_result add_into_reactor(int poll_events, IoSentryPtr sentry);
    void del_from_reactor(int poll_events, Task* tk);

    // single thread called in io_wait
    void reactor_trigger(int poll_events, TriggerSet & output);

private:
    void del_events(int poll_events);

    void trigger_task_list(TaskWSet & tasks, int events, TriggerSet & output);

    void clear_expired_sentry();

    TaskWSet& ChooseSet(int events);

    void set_pending_events(int events);

    int GetEpollFd();

    // debugger interface
public:
    std::string GetDebugInfo();

private:
#if LIBGO_SINGLE_THREAD
    typedef LFLock mutex_t;
#else
    typedef std::mutex mutex_t;
#endif
    mutex_t lock_;
    bool is_initialize_ = false;
    bool is_socket_ = false;
    bool is_epoll_ = false;
    bool sys_nonblock_ = false;
    bool user_nonblock_ = false;
    bool closed_ = false;
    bool et_mode_ = false;
    int fd_ = -1;
    int pending_events_ = 0;

    //多线程模式下, 这两个标记不保证完全准确, read_write_mode中使用poll修正
    bool readable_ = false;
    bool writable_ = false;

    timeval recv_o_ = {0, 0};
    timeval send_o_ = {0, 0};
    TaskWSet i_tasks_;
    TaskWSet o_tasks_;
    TaskWSet io_tasks_;

    LFLock epoll_fd_mtx_;
    int epoll_fd_ = -1;
    pid_t owner_pid_ = -1;
};

class FdManager
{
public:
    typedef std::pair<FdCtxPtr*, LFLock> FdPair;
    typedef std::deque<FdPair> FdDeque;
    typedef std::map<int, FdPair> BigFdMap;

    static FdManager& getInstance();

    FdCtxPtr get_fd_ctx(int fd);

    bool dup(int src, int dst);

    int close(int fd, bool call_syscall = true);

private:
    FdPair & get_pair(int fd);

    FdCtxPtr get(FdPair & fpair, int fd);

    // debugger interface
public:
    std::string GetDebugInfo();

private:
    LFLock deque_lock_;
    FdDeque fd_deque_;

    LFLock map_lock_;
    BigFdMap big_fds_;
};

inline bool FileDescriptorCtx::readable()
{
    return readable_;
}
inline bool FileDescriptorCtx::writable()
{
    return writable_;
}
inline void FileDescriptorCtx::set_readable(bool b)
{
    readable_ = b;
    DebugPrint(dbg_fd_ctx, "fd(%d) [%p] set readable %d.", fd_, this, b);
}
inline void FileDescriptorCtx::set_writable(bool b)
{
    writable_ = b;
    DebugPrint(dbg_fd_ctx, "fd(%d) [%p] set writable %d.", fd_, this, b);
}

} //namespace co 
