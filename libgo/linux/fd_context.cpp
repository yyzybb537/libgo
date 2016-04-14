#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <assert.h>
#include "fd_context.h"
#include "task.h"
#include "scheduler.h"
#include "linux_glibc_hook.h"

namespace co {

IoSentry::IoSentry(Task* tk, pollfd *fds, nfds_t nfds)
    : watch_fds_(fds, fds + nfds), task_ptr_(SharedFromThis(tk))
{
    io_state_ = pending;
    for (auto &pfd : watch_fds_)
        pfd.revents = 0;
}
IoSentry::~IoSentry()
{
    for (auto &pfd : watch_fds_)
    {
        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(pfd.fd);
        if (fd_ctx)
            fd_ctx->del_from_reactor(pfd.events, task_ptr_.get());
    }
}
bool IoSentry::switch_state_to_triggered()
{
    long expected = pending;
    return std::atomic_compare_exchange_strong(&io_state_, &expected, (long)triggered);
}

FileDescriptorCtx::FileDescriptorCtx(int fd)
    : fd_(fd)
{
    struct stat fd_stat;
    if (-1 == fstat(fd_, &fd_stat)) {
        is_initialize_ = false;
        is_socket_ = false;
    } else {
        is_initialize_ = true;
        is_socket_ = S_ISSOCK(fd_stat.st_mode);
    }

    if (is_socket_) {
        int flags = fcntl_f(fd_, F_GETFL, 0);
        if (!(flags & O_NONBLOCK))
            fcntl_f(fd_, F_SETFL, flags | O_NONBLOCK);

        sys_nonblock_ = true;
//        recv_o_ = send_o_ = timeval{0, 0};
//        getsockopt_f()
    } else {
        sys_nonblock_ = false;
        recv_o_ = send_o_ = timeval{0, 0};
    }

    user_nonblock_ = false;
    closed_ = false;
    pending_events_ = 0;
    DebugPrint(dbg_fd_ctx, "fd(%p:%d) context construct. "
            "is_socket(%d) sys_nonblock(%d) user_nonblock(%d)",
            this, fd_, (int)is_socket_, (int)sys_nonblock_, (int)user_nonblock_);
}
FileDescriptorCtx::~FileDescriptorCtx()
{
    assert(i_tasks_.empty());
    assert(o_tasks_.empty());
    assert(io_tasks_.empty());
    assert(pending_events_ == 0);
    assert(closed_);
    DebugPrint(dbg_fd_ctx, "fd(%p:%d) context destruct", this, fd_);
}

bool FileDescriptorCtx::is_socket()
{
    return is_socket_;
}
bool FileDescriptorCtx::closed()
{
    return closed_;
}
int FileDescriptorCtx::close(bool call_syscall)
{
    std::unique_lock<std::mutex> lock(lock_);
    if (closed()) return 0;
    DebugPrint(dbg_fd_ctx, "close fd(%p:%d) call_syscall:%d", this, fd_, (int)call_syscall);
    closed_ = true;
    set_pending_events(0);
    int ret = 0;
    if (call_syscall)
        ret = close_f(fd_);

    TaskWSet *tasks_arr[3] = {&i_tasks_, &o_tasks_, &io_tasks_};
    for (int i = 0; i < 3; ++i)
    {
        TaskWSet & tasks = *tasks_arr[i];
        for (auto &kv : tasks)
        {
            IoSentryPtr sptr = kv.second.lock();
            if (!sptr) continue;
            DebugPrint(dbg_fd_ctx, "close fd(%p:%d) trigger task(%s)", this, fd_,
                    sptr->task_ptr_->DebugInfo());
            for (auto &pfd : sptr->watch_fds_)
                if (pfd.fd == fd_)
                    pfd.revents = POLLNVAL;
            g_Scheduler.GetIoWait().IOBlockTriggered(sptr);
        }
        tasks.clear();
    }

    return ret;
}
void FileDescriptorCtx::set_user_nonblock(bool b)
{
    user_nonblock_ = b;
}
bool FileDescriptorCtx::user_nonblock()
{
    return user_nonblock_;
}
void FileDescriptorCtx::set_sys_nonblock(bool b)
{
    sys_nonblock_ = b;
}
bool FileDescriptorCtx::sys_nonblock()
{
    return sys_nonblock_;
}
void FileDescriptorCtx::set_time_o(int type, timeval const& tv)
{
    std::unique_lock<std::mutex> lock(lock_);
    if (type == SO_RCVTIMEO)
        recv_o_ = tv;
    else
        send_o_ = tv;
}
void FileDescriptorCtx::get_time_o(int type, timeval *tv)
{
    std::unique_lock<std::mutex> lock(lock_);
    if (type == SO_RCVTIMEO)
        *tv = recv_o_;
    else
        *tv = send_o_;
}
bool FileDescriptorCtx::add_into_reactor(int poll_events, IoSentryPtr sentry)
{
    std::unique_lock<std::mutex> lock(lock_);
    if (closed()) return false;

    DebugPrint(dbg_fd_ctx,
            "task(%s) add_into_reactor fd(%p:%d) poll_events(%d) pending_events(%d)",
            sentry->task_ptr_->DebugInfo(), this, fd_, poll_events, pending_events_);

    TaskWSet &tk_set = ChooseSet(poll_events);

    poll_events &= (POLLIN | POLLOUT);  // strip err, hup, rdhup ...
    if (poll_events & ~pending_events_) {
        uint32_t events = pending_events_ | poll_events;
        if (!pending_events_) {
            // 之前不再epoll中, 使用ADD添加
            if (-1 == g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_ADD, fd_, events,
                        is_socket()))
                return false;
        } else {
            // 之前在epoll中, 使用MOD修改关注的事件
            int res = g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_MOD, fd_, events,
                    is_socket());
            if (res == -1) {
                if (errno == ENOENT) {
                    assert(false);  // add和del之间有锁在控制, 不应该走到这里.
                }

                return false;
            }
        }

        set_pending_events(events);
        DebugPrint(dbg_fd_ctx, "fd(%p:%d) add to events(%d)", this, fd_, pending_events_);
    }

    // add_into_reactor只会在协程中执行, 执行即表示旧的已失效, 所以可以直接覆盖.
    tk_set[sentry->task_ptr_.get()] = sentry;
    return true;
}
void FileDescriptorCtx::del_from_reactor(int poll_events, Task* tk)
{
    std::unique_lock<std::mutex> lock(lock_);
    if (closed()) return;

    DebugPrint(dbg_fd_ctx,
            "del_from_reactor fd(%p:%d) poll_events(%d) pending_events(%d)",
            this, fd_, poll_events, pending_events_);

    if (!pending_events_) return;

    TaskWSet &tk_set = ChooseSet(poll_events);
    if (!tk_set.erase(tk)) return ;

    // 同一个fd允许被多个协程等待, 但不会被很多个协程等待, 所以此处可以遍历.
    // 2016-04-13 16:35:34 @yyz IoSentry析构时会来删除对应的weak_ptr
//    clear_expired_sentry();
    if (!tk_set.empty()) return ;

    // 所在等待队列空了, 检测是否可以减少监听的事件
    int del_event = 0;
    if (&tk_set == &i_tasks_) {
        // POLLIN event
        if (io_tasks_.empty())
            del_event = POLLIN;
    } else if (&tk_set == &o_tasks_) {
        if (io_tasks_.empty())
            del_event = POLLOUT;
    } else if (&tk_set == &io_tasks_) {
        if (i_tasks_.empty())
            del_event |= POLLIN;
        if (o_tasks_.empty())
            del_event |= POLLOUT;
    }

    if (del_event)
        del_events(del_event);
}
void FileDescriptorCtx::del_events(int poll_events)
{
    int new_pending_event = pending_events_ & ~poll_events;
    DebugPrint(dbg_fd_ctx, "fd(%p:%d) del_events(%d) pending(%d) new_pending(%d)",
            this, fd_, poll_events, pending_events_, new_pending_event);
    if (new_pending_event == pending_events_) return ;

    if (new_pending_event) {
        // 还有事件要监听, MOD
        if (-1 == g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_MOD, fd_,
                    new_pending_event, is_socket()))
        {
            DebugPrint(dbg_fd_ctx, "fd(%p:%d) epoll_ctl_mod(events:%d) error:%s",
                    this, fd_, new_pending_event, strerror(errno));
        }
    } else {
        // 没有需要监听的事件了, 可以DEL了
        if (-1 == g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_DEL, fd_, 0,
                    is_socket())) {
            DebugPrint(dbg_fd_ctx, "fd(%p:%d) epoll_ctl_del error:%s", this, fd_,
                    strerror(errno));
        }
    }

    set_pending_events(new_pending_event);
}

void FileDescriptorCtx::clear_expired_sentry()
{
    TaskWSet *tasks_arr[3] = {&i_tasks_, &o_tasks_, &io_tasks_};
    for (int i = 0; i < 3; ++i)
    {
        TaskWSet & tasks = *tasks_arr[i];
        auto it = tasks.begin();
        while (it != tasks.end()) {
            if (it->second.expired())
                it = tasks.erase(it);
            else
                ++it;
        }
    }
}

void FileDescriptorCtx::reactor_trigger(int poll_events, TriggerSet & output)
{
    std::unique_lock<std::mutex> lock(lock_);
    if (closed()) return;

    DebugPrint(dbg_fd_ctx,
            "reactor_trigger fd(%p:%d) poll_events(%d) pending_events(%d)",
            this, fd_, poll_events, pending_events_);

    if (poll_events & ~(POLLIN | POLLOUT)) {
        // has error
        DebugPrint(dbg_fd_ctx, "fd(%p:%d) trigger with error", this, fd_);
        trigger_task_list(i_tasks_, poll_events, output);
        trigger_task_list(o_tasks_, poll_events, output);
        trigger_task_list(io_tasks_, poll_events, output);
        del_events(POLLIN | POLLOUT);
    } else {
        if (poll_events & POLLIN) {
            // readable
            DebugPrint(dbg_fd_ctx, "fd(%p:%d) trigger with readable", this, fd_);
            trigger_task_list(i_tasks_, poll_events, output);
            trigger_task_list(io_tasks_, poll_events, output);
        } 
        
        if (poll_events & POLLOUT) {
            // writable
            DebugPrint(dbg_fd_ctx, "fd(%p:%d) trigger with writable", this, fd_);
            trigger_task_list(o_tasks_, poll_events, output);
            trigger_task_list(io_tasks_, poll_events, output);
        }

        del_events(poll_events);
    }
}

void FileDescriptorCtx::trigger_task_list(TaskWSet & tasks,
        int events, TriggerSet & output)
{
    auto self = this->shared_from_this();
    for (auto & kv : tasks)
    {
        IoSentryPtr sptr = kv.second.lock();
        if (!sptr) continue;

        for (auto &pfd : sptr->watch_fds_)
            if (pfd.fd == fd_) {
                if (!(events & (POLLIN | POLLOUT)))
                    pfd.revents = events & ~(POLLIN | POLLOUT);
                else if (!(events & POLLIN))
                    pfd.revents = events & ~POLLIN;
                else if (!(events & POLLOUT))
                    pfd.revents = events & ~POLLOUT;
                else
                    pfd.revents = events;
            }
        output.insert(sptr);
    }

    tasks.clear();
}

FileDescriptorCtx::TaskWSet& FileDescriptorCtx::ChooseSet(int events)
{
    events &= (POLLIN | POLLOUT);
    if (events == POLLIN) {
        return i_tasks_;
    } else if (events == POLLOUT) {
        return o_tasks_;
    } else {
        return io_tasks_;
    }
}
void FileDescriptorCtx::set_pending_events(int events)
{
    DebugPrint(dbg_fd_ctx, "fd(%p:%d) switch pending from (%d) to (%d)",
            this, fd_, pending_events_, events);
    pending_events_ = events;
}

FdManager & FdManager::getInstance()
{
    static FdManager obj;
    return obj;
}
FdCtxPtr FdManager::get_fd_ctx(int fd)
{
    if (fd < 0) return FdCtxPtr();

    std::unique_lock<LFLock> lock(lock_);

    FdCtxPtr* & pptr = get_ref(fd);
    if (!pptr)
        pptr =  new FdCtxPtr(new FileDescriptorCtx(fd));

    return *pptr;
}
int FdManager::close(int fd, bool call_syscall)
{
    if (fd < 0) return 0;

    std::unique_lock<LFLock> lock(lock_);

    FdCtxPtr* & pptr = get_ref(fd);
    if (!pptr) {
        if (call_syscall)
            return close_f(fd);
        return 0;
    }

    int ret = (*pptr)->close(call_syscall);
    delete pptr;
    pptr = nullptr;
    return ret;
}
bool FdManager::dup(int src, int dst)
{
    std::unique_lock<LFLock> lock(lock_);

    FdCtxPtr* & src_ptr = get_ref(src);
    if (!src_ptr)
        src_ptr = new FdCtxPtr(new FileDescriptorCtx(src));

    FdCtxPtr* & dst_ptr = get_ref(dst);
    if (dst_ptr) return false;

    dst_ptr = new FdCtxPtr(*src_ptr);
    return true;
}

FdCtxPtr*& FdManager::get_ref(int fd)
{
    if (fd >= 8000000 || fd < 0)
        return big_fds_[fd];

    if ((int)fd_list_.size() <= fd)
        fd_list_.resize(fd + 1);

    return fd_list_[fd];
}

} //namespace co
