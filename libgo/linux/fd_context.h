#pragma once
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <mutex>
#include <list>
#include <vector>
#include <set>
#include <map>
#include <atomic>
#include <memory>
#include <deque>
#include <poll.h>
#include "ts_queue.h"
#include "spinlock.h"
#include "util.h"
#include "timer.h"

namespace co {

struct Task;
typedef std::weak_ptr<Task> TaskWeakPtr;
typedef std::shared_ptr<Task> TaskPtr;

class FileDescriptorCtx;
typedef std::shared_ptr<FileDescriptorCtx> FdCtxPtr;
typedef std::weak_ptr<FileDescriptorCtx> FdCtxWeakPtr;

struct IoSentry : public RefObject, public TSQueueHook
{
    enum task_io_state
    {
        pending,
        triggered,
    };

    volatile std::atomic<long> io_state_;
    std::vector<pollfd> watch_fds_;
    CoTimerPtr timer_;
    TaskPtr task_ptr_;

    explicit IoSentry(Task* tk, pollfd *fds, nfds_t nfds);
    ~IoSentry();

    // return: cas pending to triggered
    bool switch_state_to_triggered();
};
typedef std::weak_ptr<IoSentry> IoSentryWeakPtr;
typedef std::shared_ptr<IoSentry> IoSentryPtr;
typedef std::set<IoSentryPtr> TriggerSet;

class FileDescriptorCtx
    : public std::enable_shared_from_this<FileDescriptorCtx>
{
public:
    typedef std::map<Task*, IoSentryWeakPtr> TaskWSet;

    explicit FileDescriptorCtx(int fd);
    ~FileDescriptorCtx();

    bool is_initialize();
    bool is_socket();
    bool closed();
    int close(bool call_syscall);

    void set_user_nonblock(bool b);
    bool user_nonblock();

    void set_sys_nonblock(bool b);
    bool sys_nonblock();

    // @type: SO_RCVTIMEO SO_SNDTIMEO
    void set_time_o(int type, timeval const& tv);
    void get_time_o(int type, timeval *tv);

    // multiple threads called in coroutines
    bool add_into_reactor(int poll_events, IoSentryPtr sentry);
    void del_from_reactor(int poll_events, Task* tk);

    // single thread called in io_wait
    void reactor_trigger(int poll_events, TriggerSet & output);

private:
    void del_events(int poll_events);

    void trigger_task_list(TaskWSet & tasks, int events, TriggerSet & output);

    void clear_expired_sentry();

    TaskWSet& ChooseSet(int events);

private:
    std::mutex lock_;
    bool is_initialize_;
    bool is_socket_;
    bool sys_nonblock_;
    bool user_nonblock_;
    bool closed_;
    int fd_;
    int pending_events_;
    timeval recv_o_;
    timeval send_o_;
    TaskWSet i_tasks_;
    TaskWSet o_tasks_;
    TaskWSet io_tasks_;
};

class FdManager
{
public:
    typedef std::deque<FdCtxPtr*> FdList;

    static FdManager& getInstance();

    FdCtxPtr get_fd_ctx(int fd);

    bool dup(int src, int dst);

    int close(int fd, bool call_syscall = true);

private:
    LFLock lock_;
    FdList fd_list_;
};

} //namespace co 
