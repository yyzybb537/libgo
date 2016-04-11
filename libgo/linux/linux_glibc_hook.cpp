#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <assert.h>
#include <chrono>
#include "scheduler.h"
#include "fd_context.h"
using namespace co;

namespace co {
    void coroutine_hook_init();
}

template <typename OriginF, typename ... Args>
static ssize_t read_write_mode(int fd, OriginF fn, const char* hook_fn_name, uint32_t event, int timeout_so, Args && ... args)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook %s. %s coroutine.",
            tk ? tk->DebugInfo() : "nil", hook_fn_name, g_Scheduler.IsCoroutine() ? "In" : "Not in");

    FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
    if (!fd_ctx) {
        errno = EBADF;
        return -1;
    }

    timeval tv;
    fd_ctx->get_time_o(timeout_so, &tv);
    int timeout_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    auto start = std::chrono::system_clock::now();

retry:
    ssize_t n = fn(fd, std::forward<Args>(args)...);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        int poll_timeout = 0;
        if (!timeout_ms)
            poll_timeout = -1;
        else {
            int expired = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - start).count();
            if (expired > timeout_ms) {
                errno = EAGAIN;
                return -1;  // 已超时
            }

            // 剩余的等待时间
            poll_timeout = timeout_ms - expired;
        }

        pollfd pfd;
        pfd.fd = fd;
        pfd.events = event | POLLERR | POLLHUP;
        int triggers = poll(&pfd, 1, poll_timeout);
        if (-1 == triggers) {
            return -1;
        } else if (0 == triggers) {  // poll等待超时
            errno = EAGAIN;
            return -1;
        }

        goto retry;     // 事件触发 OR epoll惊群效应
    }

    return n;
}

template <typename OriginF, typename ... Args>
static ssize_t OLD_READ_WRITE_MODE(int fd, OriginF fn, const char* hook_fn_name, uint32_t event, int timeout_so, Args && ... args)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook %s. %s coroutine.",
            tk ? tk->DebugInfo() : "nil", hook_fn_name, g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return fn(fd, std::forward<Args>(args)...);

    struct stat fd_stat;
    if (-1 == fstat(fd, &fd_stat))
        return fn(fd, std::forward<Args>(args)...);

    if (!S_ISSOCK(fd_stat.st_mode)) // 不是socket, 不HOOK.
        return fn(fd, std::forward<Args>(args)...);

    int flags = fcntl(fd, F_GETFL, 0);
    if (-1 == flags || (flags & O_NONBLOCK))
        return fn(fd, std::forward<Args>(args)...);

    if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK))
        return fn(fd, std::forward<Args>(args)...);

    DebugPrint(dbg_hook, "task(%s) real hook %s fd=%d", tk->DebugInfo(), hook_fn_name, fd);
    ssize_t n = fn(fd, std::forward<Args>(args)...);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // get timeout option.
        int timeout_ms = -1;
        struct timeval timeout;
        socklen_t timeout_blen = sizeof(timeout);
        if (0 == getsockopt(fd, SOL_SOCKET, timeout_so, &timeout, &timeout_blen)) {
            if (timeout.tv_sec > 0 || timeout.tv_usec > 0) {
                timeout_ms = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
                DebugPrint(dbg_hook, "hook task(%s) %s timeout=%dms. fd=%d",
                        g_Scheduler.GetCurrentTaskDebugInfo(), hook_fn_name, timeout_ms, fd);
            }
        }
        auto start_time = std::chrono::system_clock::now();

retry:
        // add into epoll, and switch other context.
        g_Scheduler.IOBlockSwitch(fd, event, timeout_ms);
        bool is_timeout = false;
        if (tk->GetIoWaitData().io_block_timer_) {
            is_timeout = true;
            if (g_Scheduler.BlockCancelTimer(tk->GetIoWaitData().io_block_timer_)) {
                is_timeout = false;
                tk->DecrementRef(); // timer use ref.
            }
        }

        if (tk->GetIoWaitData().wait_successful_ == 0) {
            if (is_timeout) {
                fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
                errno = EAGAIN;
                return -1;
            } else {
                fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
                return fn(fd, std::forward<Args>(args)...);
            }
        }

        DebugPrint(dbg_hook, "continue task(%s) %s. fd=%d", g_Scheduler.GetCurrentTaskDebugInfo(), hook_fn_name, fd);
        n = fn(fd, std::forward<Args>(args)...);
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /// epoll误唤醒
            if (timeout_ms == -1)   // 不超时, 继续重试
                goto retry;

            int delay_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - start_time).count();
            if (delay_ms < timeout_ms) {    // 还有剩余的超时时间, 重试一次
                timeout_ms -= delay_ms;
                goto retry;
            }
        }
    } else {
        DebugPrint(dbg_hook, "task(%s) syscall(%s) completed immediately. fd=%d",
                g_Scheduler.GetCurrentTaskDebugInfo(), hook_fn_name, fd);
    }

    int e = errno;
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    errno = e;
    return n;
}

extern "C" {

typedef int(*connect_t)(int, const struct sockaddr *, socklen_t);
static connect_t connect_f = NULL;

typedef ssize_t(*read_t)(int, void *, size_t);
static read_t read_f = NULL;

typedef ssize_t(*readv_t)(int, const struct iovec *, int);
static readv_t readv_f = NULL;

typedef ssize_t(*recv_t)(int sockfd, void *buf, size_t len, int flags);
static recv_t recv_f = NULL;

typedef ssize_t(*recvfrom_t)(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen);
static recvfrom_t recvfrom_f = NULL;

typedef ssize_t(*recvmsg_t)(int sockfd, struct msghdr *msg, int flags);
static recvmsg_t recvmsg_f = NULL;

typedef ssize_t(*write_t)(int, const void *, size_t);
static write_t write_f = NULL;

typedef ssize_t(*writev_t)(int, const struct iovec *, int);
static writev_t writev_f = NULL;

typedef ssize_t(*send_t)(int sockfd, const void *buf, size_t len, int flags);
static send_t send_f = NULL;

typedef ssize_t(*sendto_t)(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen);
static sendto_t sendto_f = NULL;

typedef ssize_t(*sendmsg_t)(int sockfd, const struct msghdr *msg, int flags);
static sendmsg_t sendmsg_f = NULL;

typedef int(*poll_t)(struct pollfd *fds, nfds_t nfds, int timeout);
static poll_t poll_f = NULL;

typedef int(*select_t)(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, struct timeval *timeout);
static select_t select_f = NULL;

typedef int(*accept_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
static accept_t accept_f = NULL;

typedef unsigned int(*sleep_t)(unsigned int seconds);
static sleep_t sleep_f = NULL;

typedef int(*nanosleep_t)(const struct timespec *req, struct timespec *rem);
static nanosleep_t nanosleep_f = NULL;

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook connect. %s coroutine.",
            tk ? tk->DebugInfo() : "nil", g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk) {
        return connect_f(fd, addr, addrlen);
    } else {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags & O_NONBLOCK)
            return connect_f(fd, addr, addrlen);

        if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK))
            return connect_f(fd, addr, addrlen);

        int n = connect_f(fd, addr, addrlen);
        int e = errno;
        if (n == 0) {
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            DebugPrint(dbg_hook, "continue task(%s) connect completed immediately. fd=%d",
                    g_Scheduler.GetCurrentTaskDebugInfo(), fd);
            return 0;
        } else if (n != -1 || errno != EINPROGRESS) {
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            errno = e;
            return n;
        } else {
            // add into epoll, and switch other context.
            g_Scheduler.IOBlockSwitch(fd, EPOLLOUT, -1);
        }

        if (tk->GetIoWaitData().wait_successful_ == 0) {
            // 添加到epoll中失败了
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            errno = e;
            return n;
        }

        DebugPrint(dbg_hook, "continue task(%s) connect. fd=%d", g_Scheduler.GetCurrentTaskDebugInfo(), fd);
        int error = 0;
        socklen_t len = sizeof(int);
        if (0 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
            if (0 == error) {
                fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
                DebugPrint(dbg_hook, "continue task(%s) connect success async. fd=%d",
                        g_Scheduler.GetCurrentTaskDebugInfo(), fd);
                return 0;
            } else {
                fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
                errno = error;
                return -1;
            }
        }

        e = errno;      // errno set by getsockopt.
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        errno = e;
        return -1;
    }
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!accept_f) coroutine_hook_init();
    return read_write_mode(sockfd, accept_f, "accept", POLLIN, SO_RCVTIMEO, addr, addrlen);
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (!read_f) coroutine_hook_init();
    return read_write_mode(fd, read_f, "read", POLLIN, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    if (!readv_f) coroutine_hook_init();
    return read_write_mode(fd, readv_f, "readv", POLLIN, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    if (!recv_f) coroutine_hook_init();
    return read_write_mode(sockfd, recv_f, "recv", POLLIN, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen)
{
    if (!recvfrom_f) coroutine_hook_init();
    return read_write_mode(sockfd, recvfrom_f, "recvfrom", POLLIN, SO_RCVTIMEO, buf, len, flags,
            src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    if (!recvmsg_f) coroutine_hook_init();
    return read_write_mode(sockfd, recvmsg_f, "recvmsg", POLLIN, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (!write_f) coroutine_hook_init();
    return read_write_mode(fd, write_f, "write", POLLOUT, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!writev_f) coroutine_hook_init();
    return read_write_mode(fd, writev_f, "writev", POLLOUT, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    if (!send_f) coroutine_hook_init();
    return read_write_mode(sockfd, send_f, "send", POLLOUT, SO_SNDTIMEO, buf, len, flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (!sendto_f) coroutine_hook_init();
    return read_write_mode(sockfd, sendto_f, "sendto", POLLOUT, SO_SNDTIMEO, buf, len, flags,
            dest_addr, addrlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    if (!sendmsg_f) coroutine_hook_init();
    return read_write_mode(sockfd, sendmsg_f, "sendmsg", POLLOUT, SO_SNDTIMEO, msg, flags);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!poll_f) coroutine_hook_init();

    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook poll(nfds=%d, timeout=%d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil",
            (int)nfds, timeout,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return poll_f(fds, nfds, timeout);

    if (timeout == 0)
        return poll_f(fds, nfds, timeout);

    if (nfds == 0) {
        // co sleep
        g_Scheduler.SleepSwitch(timeout);
        return 0;
    }

    // test nonblock poll
    int res = poll_f(fds, nfds, 0);
    if (res != 0)
        return res;

    // create io-sentry
    IoSentryPtr io_sentry = MakeShared<IoSentry>(tk, fds, nfds);

    // add file descriptor into epoll or poll.
    bool added = false;
    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0;     // clear revents
        pollfd & pfd = io_sentry->watch_fds_[i];
        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(pfd.fd);
        if (!fd_ctx || fd_ctx->closing()) {
            // bad file descriptor
            pfd.revents = POLLNVAL;
            continue;
        }
        if (!fd_ctx->add_into_reactor(pfd.events, io_sentry)) {
            pfd.revents = POLLNVAL;
            continue;
        }

        added = true;
    }

    if (!added) {
        errno = 0;
        return nfds;
    }

    // set timer
    if (timeout > 0)
        io_sentry->timer_ = g_Scheduler.ExpireAt(
                std::chrono::milliseconds(timeout),
                [io_sentry]{
                    g_Scheduler.IOBlockTriggered(io_sentry);
                });

    // save io-sentry
    tk->io_sentry_ = io_sentry;

    // yield
    g_Scheduler.IOBlockSwitch();

    // clear task->io_sentry_ reference count
    tk->io_sentry_.reset();

    if (io_sentry->timer_) {
        g_Scheduler.CancelTimer(io_sentry->timer_);
        io_sentry->timer_.reset();
    }

    int n = 0;
    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = io_sentry->watch_fds_[i].revents;
        if (fds[i].revents) ++n;
    }
    errno = 0;
    return n;
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, struct timeval *timeout)
{
//    if (!select_f) coroutine_hook_init();
//
//    int timeout_ms = -1;
//    if (timeout)
//        timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
//
//    Task* tk = g_Scheduler.GetCurrentTask();
//    DebugPrint(dbg_hook, "task(%s) hook select(nfds=%d, rd_set=%p, wr_set=%p, er_set=%p, timeout=%d ms).",
//            tk ? tk->DebugInfo() : "nil",
//            (int)nfds, readfds, writefds, exceptfds, timeout_ms);
//
//    if (!tk)
//        return select_f(nfds, readfds, writefds, exceptfds, timeout);
//
//    if (timeout_ms == 0)
//        return select_f(nfds, readfds, writefds, exceptfds, timeout);
//
//    if (!nfds && !readfds && !writefds && !exceptfds && timeout) {
//        g_Scheduler.SleepSwitch(timeout_ms);
//        return 0;
//    }
//
//    nfds = std::min<int>(nfds, FD_SETSIZE);
//    std::pair<fd_set*, uint32_t> sets[3] =
//    {
//        {readfds, EPOLLIN | EPOLLERR | EPOLLHUP},
//        {writefds, EPOLLOUT},
//        {exceptfds, EPOLLERR | EPOLLHUP}
//    };
//
//    static const char* set_names[] = {"readfds", "writefds", "exceptfds"};
//    std::vector<FdStruct> fdsts;
//    for (int i = 0; i < nfds; ++i) {
//        FdStruct *fdst = NULL;
//        for (int si = 0; si < 3; ++si) {
//            if (!sets[si].first)
//                continue;
//
//            if (!FD_ISSET(i, sets[si].first))
//                continue;
//
//            if (!fdst) {
//                fdsts.emplace_back();
//                fdst = &fdsts.back();
//                fdst->fd = i;
//            }
//
//            fdsts.back().event |= sets[si].second;
//            DebugPrint(dbg_hook, "task(%s) hook select %s(%d)",
//                    tk->DebugInfo(), set_names[si], (int)i);
//        }
//    }
//
//    g_Scheduler.IOBlockSwitch(std::move(fdsts), timeout_ms);
//    bool is_timeout = false;
//    if (tk->GetIoWaitData().io_block_timer_) {
//        is_timeout = true;
//        if (g_Scheduler.BlockCancelTimer(tk->GetIoWaitData().io_block_timer_)) {
//            is_timeout = false;
//            tk->DecrementRef(); // timer use ref.
//        }
//    }
//
//    if (tk->GetIoWaitData().wait_successful_ == 0) {
//        if (is_timeout) {
//            if (readfds) FD_ZERO(readfds);
//            if (writefds) FD_ZERO(writefds);
//            if (exceptfds) FD_ZERO(exceptfds);
//            errno = 0;
//            return 0;
//        } else {
//            if (timeout_ms > 0)
//                g_Scheduler.SleepSwitch(timeout_ms);
//            timeval immedaitely = {0, 0};
//            return select_f(nfds, readfds, writefds, exceptfds, &immedaitely);
//        }
//    }
//
//    int n = 0;
//    for (auto &fdst : tk->GetIoWaitData().wait_fds_) {
//        int fd = fdst.fd;
//        for (int si = 0; si < 3; ++si) {
//            if (!sets[si].first)
//                continue;
//
//            if (!FD_ISSET(fd, sets[si].first))
//                continue;
//
//            if (sets[si].second & fdst.epoll_ptr.revent) {
//                ++n;
//                continue;
//            }
//
//            FD_CLR(fd, sets[si].first);
//        }
//    }
//
//    return n;
}

unsigned int sleep(unsigned int seconds)
{
    if (!sleep_f) coroutine_hook_init();

    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook sleep(seconds=%u). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", seconds,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!g_Scheduler.IsCoroutine())
        return sleep_f(seconds);

    int timeout_ms = seconds * 1000;
    g_Scheduler.SleepSwitch(timeout_ms);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!nanosleep_f) coroutine_hook_init();

    Task* tk = g_Scheduler.GetCurrentTask();
    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
    DebugPrint(dbg_hook, "task(%s) hook nanosleep(milliseconds=%d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", timeout_ms,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!g_Scheduler.IsCoroutine())
        return nanosleep_f(req, rem);

    g_Scheduler.SleepSwitch(timeout_ms);
    return 0;
}

#if !defined(CO_DYNAMIC_LINK)
extern int __connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
extern ssize_t __read(int fd, void *buf, size_t count);
extern ssize_t __readv(int fd, const struct iovec *iov, int iovcnt);
extern ssize_t __recv(int sockfd, void *buf, size_t len, int flags);
extern ssize_t __recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen);
extern ssize_t __recvmsg(int sockfd, struct msghdr *msg, int flags);
extern ssize_t __write(int fd, const void *buf, size_t count);
extern ssize_t __writev(int fd, const struct iovec *iov, int iovcnt);
extern ssize_t __send(int sockfd, const void *buf, size_t len, int flags);
extern ssize_t __sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen);
extern ssize_t __sendmsg(int sockfd, const struct msghdr *msg, int flags);
extern int __libc_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern int __poll(struct pollfd *fds, nfds_t nfds, int timeout);
extern int __select(int nfds, fd_set *readfds, fd_set *writefds,
                          fd_set *exceptfds, struct timeval *timeout);
extern unsigned int __sleep(unsigned int seconds);
extern int __nanosleep(const struct timespec *req, struct timespec *rem);
#endif
}

namespace co
{

void coroutine_hook_init()
{
    static bool coroutine_hook_inited = false;
    if (coroutine_hook_inited) return ;

#if defined(CO_DYNAMIC_LINK)
    connect_f = (connect_t)dlsym(RTLD_NEXT, "connect");
    read_f = (read_t)dlsym(RTLD_NEXT, "read");
    readv_f = (readv_t)dlsym(RTLD_NEXT, "readv");
    recv_f = (recv_t)dlsym(RTLD_NEXT, "recv");
    recvfrom_f = (recvfrom_t)dlsym(RTLD_NEXT, "recvfrom");
    recvmsg_f = (recvmsg_t)dlsym(RTLD_NEXT, "recvmsg");
    write_f = (write_t)dlsym(RTLD_NEXT, "write");
    writev_f = (writev_t)dlsym(RTLD_NEXT, "writev");
    send_f = (send_t)dlsym(RTLD_NEXT, "send");
    sendto_f = (sendto_t)dlsym(RTLD_NEXT, "sendto");
    sendmsg_f = (sendmsg_t)dlsym(RTLD_NEXT, "sendmsg");
    accept_f = (accept_t)dlsym(RTLD_NEXT, "accept");
    poll_f = (poll_t)dlsym(RTLD_NEXT, "poll");
    select_f = (select_t)dlsym(RTLD_NEXT, "select");
    sleep_f = (sleep_t)dlsym(RTLD_NEXT, "sleep");
    nanosleep_f = (nanosleep_t)dlsym(RTLD_NEXT, "nanosleep");
#else
    connect_f = &__connect;
    read_f = &__read;
    readv_f = &__readv;
    recv_f = &__recv;
    recvfrom_f = &__recvfrom;
    recvmsg_f = &__recvmsg;
    write_f = &__write;
    writev_f = &__writev;
    send_f = &__send;
    sendto_f = &__sendto;
    sendmsg_f = &__sendmsg;
    accept_f = &__libc_accept;
    poll_f = &__poll;
    select_f = &__select;
    sleep_f = &__sleep;
    nanosleep_f = &__nanosleep;
#endif

    if (!connect_f || !read_f || !write_f || !readv_f || !writev_f || !send_f
            || !sendto_f || !sendmsg_f || !accept_f || !poll_f || !select_f
            || !sleep_f || !nanosleep_f) {
        fprintf(stderr, "Hook syscall failed. Please don't remove libc.a when static-link.\n");
        exit(1);
    }

    coroutine_hook_inited = true;
}

} //namespace co
