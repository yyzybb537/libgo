#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <assert.h>
#include "fd_context.h"
#include "task.h"
#include "scheduler.h"

extern "C" {
    typedef int(*close_t)(int);
    extern close_t close_f = nullptr;

    typedef int(*fcntl_t)(int __fd, int __cmd, ...);
    extern fcntl_t fcntl_f = nullptr;
    //TODO: dup dup2
}

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
        if (fd_ctx) {
            fd_ctx->del_from_reactor(pfd.events, task_ptr_.get());
        }
    }
}
bool IoSentry::switch_state_to_triggered()
{
    long expected = pending;
    return std::atomic_compare_exchange_strong(&io_state_, &expected, waiting);
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
    } else
        sys_nonblock_ = false;

    user_nonblock_ = false;
    closing_ = false;
    pending_events_ = 0;
    recv_o_ = send_o_ = timeval{0, 0};
}
FileDescriptorCtx::~FileDescriptorCtx()
{
    assert(i_tasks_.empty());
    assert(o_tasks_.empty());
    assert(io_tasks_.empty());
    assert(pending_events_ == 0);
    assert(closing_);
    if (close_f)
        close_f(fd_);
    else
        ::close(fd_);
}

bool FileDescriptorCtx::is_socket()
{
    return is_socket_;
}
bool FileDescriptorCtx::closing()
{
    return closing_;
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

    TaskWSet &tk_set = ChooseSet(poll_events);

    poll_events &= (POLLIN | POLLOUT);  // strip err, hup, rdhup ...
    if (poll_events & ~pending_events_) {
        uint32_t events = pending_events_ | poll_events;
        if (!pending_events_) {
            // 之前不再epoll中, 使用ADD添加
            if (0 == g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_ADD, fd_, events,
                        is_socket())) {
                pending_events_ = events;
            } else
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
            } else {
                pending_events_ = events;
            }
        }

        pending_events_ |= poll_events;
    }

    // add_into_reactor只会在协程中执行, 执行即表示旧的已失效, 所以可以直接覆盖.
    tk_set[sentry->task_ptr_.get()] = sentry;
    return true;
}
void FileDescriptorCtx::del_from_reactor(int poll_events, Task* tk)
{
    std::unique_lock<std::mutex> lock(lock_);

    if (!pending_events_) return;

    TaskWSet &tk_set = ChooseSet(poll_events);
    if (!tk_set.erase(tk)) return ;

    // 同一个fd允许被多个协程等待, 但不会被很多个协程等待, 所以此处可以遍历.
    clear_expired_sentry();
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
    int pending_event = pending_events_ & ~poll_events;
    if (pending_event == pending_events_) return ;

    if (pending_event) {
        // 还有事件要监听, MOD
        if (-1 == g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_MOD, fd_,
                    pending_event, is_socket()))
        {
            DebugPrint(dbg_ioblock, "fd(%d) epoll_ctl_mod(events:%d) error:%s",
                    fd_, pending_event, strerror(errno));
        }
    } else {
        // 没有需要监听的事件了, 可以DEL了
        if (-1 == g_Scheduler.GetIoWait().reactor_ctl(EPOLL_CTL_DEL, fd_, 0,
                    is_socket())) {
            DebugPrint(dbg_ioblock, "fd(%d) epoll_ctl_del error:%s", fd_,
                    strerror(errno));
        }
    }

    pending_events_ = pending_event;
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

    if (events & (EPOLLERR | EPOLLHUP)) {
        trigger_task_list(i_tasks_, events, output);
        trigger_task_list(o_tasks_, events, output);
        trigger_task_list(io_tasks_, events, output);
    } else if (events & (EPOLLIN | EPOLLRDHUP)) {
        trigger_task_list(i_tasks_, events, output);
        trigger_task_list(io_tasks_, events, output);
    } else if (events & (EPOLLOUT)) {
        trigger_task_list(o_tasks_, events, output);
        trigger_task_list(io_tasks_, events, output);
    } else {
        assert(false);
    }
}

void FileDescriptorCtx::trigger_task_list(TaskWSet & tasks,
        int events, std::set<TaskPtr> & output)
{
    auto self = this->shared_from_this();
    for (auto & kv : tasks)
    {
        TaskPtr sptr = kv.second.lock();
        if (!sptr)
            continue;

        TaskIoInfo & io_info = sptr->io_data_.io_data_;
        assert(io_info.watch_fds_.count(self));
        auto &events_pair = io_info.watch_fds_[self];
        events_pair.second = events;
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

} //namespace co
