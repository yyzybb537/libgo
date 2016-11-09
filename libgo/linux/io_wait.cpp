#include "io_wait.h"
#ifdef __linux__
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
#include <sys/event.h>
typedef struct kevent kevent_t;
#endif
#include <libgo/scheduler.h>
#include <signal.h>

namespace co
{

IoWait::IoWait()
{
    epoll_event_size_ = 1024;
}

int& IoWait::EpollFdRef()
{
    THREAD_TLS static int epoll_fd = -1;
    return epoll_fd;
}
pid_t& IoWait::EpollOwnerPid()
{
    THREAD_TLS static pid_t owner_pid = -1;
    return owner_pid;
}

static uint32_t PollEvent2Epoll(short events)
{
    uint32_t e = 0;
    if (events & POLLIN)   e |= EPOLLIN;
    if (events & POLLOUT)  e |= EPOLLOUT;
    if (events & POLLHUP)  e |= EPOLLHUP;
    if (events & POLLERR)  e |= EPOLLERR;
    return e;
}

static short EpollEvent2Poll(uint32_t events)
{
    short e = 0;
    if (events & EPOLLIN)  e |= POLLIN;
    if (events & EPOLLOUT) e |= POLLOUT;
    if (events & EPOLLHUP) e |= POLLHUP;
    if (events & EPOLLERR) e |= POLLERR;
    return e;
}

static std::string EpollEvent2Str(uint32_t events)
{
    std::string e("|");
    if (events & EPOLLIN)  e += "POLLIN|";
    if (events & EPOLLOUT) e += "POLLOUT|";
    if (events & EPOLLHUP) e += "POLLHUP|";
    if (events & EPOLLERR) e += "POLLERR|";
#ifdef __linux__
    if (events & EPOLLET) e += "EPOLLET|";
#endif
    return e;
}

static const char* EpollMod2Str(int mod)
{
    if (mod == EPOLL_CTL_ADD)
        return "EPOLL_CTL_ADD";
    else if (mod == EPOLL_CTL_DEL)
        return "EPOLL_CTL_DEL";
    else if (mod == EPOLL_CTL_MOD)
        return "EPOLL_CTL_MOD";
    else
        return "UNKOWN";
}

void IoWait::CoSwitch()
{
    Task* tk = g_Scheduler.GetCurrentTask();
    if (!tk) return ;

    tk->state_ = TaskState::io_block;
    DebugPrint(dbg_ioblock, "task(%s) enter io_block", tk->DebugInfo());
    g_Scheduler.CoYield();
}

void IoWait::SchedulerSwitch(Task* tk)
{
    auto sentry = tk->io_sentry_;   // reference increment. avoid wakeup task at other thread.
    wait_io_sentries_.push(sentry.get()); // A
    if (sentry->io_state_ == IoSentry::triggered) // B
        __IOBlockTriggered(sentry);
}

void IoWait::IOBlockTriggered(IoSentryPtr io_sentry)
{
    if (io_sentry->switch_state_to_triggered()) // B
        __IOBlockTriggered(io_sentry);
}

void IoWait::__IOBlockTriggered(IoSentryPtr io_sentry)
{
    assert(io_sentry->io_state_ == IoSentry::triggered);
    if (wait_io_sentries_.erase(io_sentry.get())) { // A
        DebugPrint(dbg_ioblock, "task(%s) exit io_block",
                io_sentry->task_ptr_->DebugInfo());
        g_Scheduler.AddTaskRunnable(io_sentry->task_ptr_.get());
    }
}

#ifdef __linux__
int IoWait::reactor_ctl(int epollfd, int epoll_ctl_mod, int fd,
        uint32_t poll_events, bool is_support, bool et_mode)
{
    if (is_support) {
        epoll_event ev;
        ev.events = PollEvent2Epoll(poll_events);
        if (et_mode) ev.events |= EPOLLET;
        ev.data.fd = fd;
        int res = epoll_ctl(epollfd, epoll_ctl_mod, fd, &ev);
        DebugPrint(dbg_ioblock, "epoll_ctl(fd:%d, MOD:%s, events:%s) returns %d",
                fd, EpollMod2Str(epoll_ctl_mod),
                EpollEvent2Str(ev.events).c_str(), res);
        return res;
    }

    // TODO: poll模拟
    errno = EPERM;
    return -1;
}
#endif
#ifdef __APPLE__
int IoWait::reactor_ctl(int epollfd, int epoll_ctl_mod, int fd,
                           uint32_t poll_events, bool is_support, bool et_mode)
{
	if (is_support) {
		struct kevent ke;

		if ((poll_events & EPOLLIN) && (epoll_ctl_mod != EPOLL_CTL_DEL)) {
			EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
			if (kevent(epollfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
		}
		else {
			EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
			kevent(epollfd, &ke, 1, NULL, 0, NULL);
		}
		if ((poll_events & EPOLLOUT) && (epoll_ctl_mod != EPOLL_CTL_DEL)) {
			EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
			if (kevent(epollfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
		}
		else {
			EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
			kevent(epollfd, &ke, 1, NULL, 0, NULL);
		}

		return 0;
	}

	// TODO: poll模拟
	errno = EPERM;
	return -1;
}
#endif
    
int IoWait::WaitLoop(int wait_milliseconds)
{
    if (!IsEpollCreated())
        return -1;

    // TODO: epoll多线程触发, poll单线程触发.
#ifdef __linux__
    THREAD_TLS static epoll_event *evs = new epoll_event[epoll_event_size_];

retry:
    int n = epoll_wait(GetEpollFd(), evs, epoll_event_size_, wait_milliseconds);
#endif
#ifdef __APPLE__
//    /*THREAD_TLS*/ static struct kevent *evs = new kevent_t[epoll_event_size_];
    THREAD_TLS static struct kevent *evs = NULL;
    if (!evs) evs = new kevent_t[epoll_event_size_];

    struct timespec timeout;
    timeout.tv_sec = wait_milliseconds / 1000;
    timeout.tv_nsec = wait_milliseconds % 1000 * 1000000;
    
retry:
    int n = kevent(GetEpollFd(), NULL, 0, evs, epoll_event_size_, &timeout);
#endif
    if (n == -1) {
        if (errno == EINTR) {
            goto retry;
        }

        return 0;
    }

    DebugPrint(dbg_scheduler|dbg_scheduler_sleep, "epollwait(%d ms) returns: %d",
            wait_milliseconds, n);

#ifdef __linux__
    TriggerSet triggers;
    for (int i = 0; i < n; ++i)
    {
        int fd = evs[i].data.fd;
        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
        DebugPrint(dbg_ioblock, "epoll trigger fd(%d) events(%s) has_ctx(%d)",
                fd, EpollEvent2Str(evs[i].events).c_str(), !!fd_ctx);
        if (!fd_ctx) continue;

        // 暂存, 最后再执行Trigger, 以便于poll可以得到更多的事件触发.
        fd_ctx->reactor_trigger(EpollEvent2Poll(evs[i].events), triggers);
    }
#endif
    
#ifdef __APPLE__
    TriggerSet triggers;
    for (int i = 0; i < n; ++i)
    {
        int fd = evs[i].ident;
        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
//        DebugPrint(dbg_ioblock, "epoll trigger fd(%d) events(%s) has_ctx(%d)",
//                   fd, EpollEvent2Str(evs[i].events).c_str(), !!fd_ctx);
        if (!fd_ctx) continue;
        
        int mask = 0;
        if (evs[i].filter == EVFILT_READ) mask |= EPOLLIN;
        if (evs[i].filter == EVFILT_WRITE) mask |= EPOLLOUT;
        
        // 暂存, 最后再执行Trigger, 以便于poll可以得到更多的事件触发.
        fd_ctx->reactor_trigger(EpollEvent2Poll(mask), triggers);
    }
#endif

    // TODO: run poll

    // 触发事件, 唤醒等待中的协程.
    // 过时的唤醒由于已不在wait列表中, 
    // 会被IOBlockTriggered中的原子操作switch_state_to_triggered根据返回值过滤掉.
    for (auto & sentry : triggers)
        IOBlockTriggered(sentry);

    return n;
}

int IoWait::GetEpollFd()
{
    CreateEpoll();
    return EpollFdRef();
}

void IoWait::CreateEpoll()
{
    pid_t pid = getpid();
    pid_t& epoll_owner_pid = EpollOwnerPid();
    if (epoll_owner_pid == pid) return ;
    std::unique_lock<LFLock> lock(epoll_create_lock_);
    if (epoll_owner_pid == pid) return ;

    epoll_owner_pid = pid;
    epoll_event_size_ = g_Scheduler.GetOptions().epoll_event_size;

    int & epoll_fd_ = EpollFdRef();
    if (epoll_fd_ >= 0)
        close(epoll_fd_);

#ifdef __linux__
    epoll_fd_ = epoll_create(epoll_event_size_);
#endif
#ifdef __APPLE__
    epoll_fd_ = kqueue();
#endif
    if (epoll_fd_ != -1) {
        DebugPrint(dbg_ioblock, "create epoll success. epollfd=%d", epoll_fd_);
        // 使用epoll需要忽略SIGPIPE信号
        IgnoreSigPipe();
    }
    else {
        fprintf(stderr,
                "CoroutineScheduler init failed. epoll create error:%s\n",
                strerror(errno));
        exit(1);
    }
}

void IoWait::IgnoreSigPipe()
{
    DebugPrint(dbg_ioblock, "Ignore signal SIGPIPE");
    sigignore(SIGPIPE);
}

bool IoWait::IsEpollCreated()
{
    return EpollFdRef() != -1 && EpollOwnerPid() == getpid();
}

} //namespace co
