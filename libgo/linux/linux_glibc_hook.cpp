#include <dlfcn.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <assert.h>
#include <chrono>
#include <map>
#include <stdarg.h>
#include <poll.h>
#include "linux_glibc_hook.h"
#include "hook_signal.h"
#include "fd_context.h"
#include <libgo/scheduler.h>
#if WITH_CARES
#include <ares.h>
#endif
using namespace co;

// 设置阻塞式connect超时时间(-1无限时)
static thread_local int s_connect_timeout = -1;

namespace co {
    void coroutine_hook_init();

    void set_connect_timeout(int milliseconds)
    {
        s_connect_timeout = milliseconds;
    }
    void initialize_socket_async_methods(int socketfd)
    {
        FdManager::getInstance().get_fd_ctx(socketfd);
    }

    int libgo_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
    {
        Task* tk = g_Scheduler.GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) call libgo_epoll_wait. %s coroutine.",
                tk ? tk->DebugInfo() : "nil", g_Scheduler.IsCoroutine() ? "In" : "Not in");

        if (!tk)
            return epoll_wait(epfd, events, maxevents, timeout);

        int res = epoll_wait(epfd, events, maxevents, 0);
        if (res != 0) return res;

        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(epfd);
        if (!fd_ctx || fd_ctx->closed()) {
            errno = EINVAL;  // 已被close或无效的fd
            return -1;
        }
        fd_ctx->set_is_epoll();

        pollfd pfd;
        pfd.fd = epfd;
        pfd.events = POLLIN | POLLOUT;
        pfd.revents = 0;
        res = poll(&pfd, 1, timeout);
        if (res <= 0) return res;
        return epoll_wait(epfd, events, maxevents, 0);
    }

    void set_et_mode(int fd)
    {
        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
        if (!fd_ctx || fd_ctx->closed()) return ;
        fd_ctx->set_et_mode();
    }

    inline int libgo_poll(struct pollfd *fds, nfds_t nfds, int timeout, bool nonblocking_check)
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

        // --------------------------------
        // 全部是负数fd时, 等价于sleep
        nfds_t negative_fd_n = 0;
        for (nfds_t i = 0; i < nfds; ++i)
            if (fds[i].fd < 0)
                ++ negative_fd_n;

        if (nfds == negative_fd_n) {
            // co sleep
            g_Scheduler.SleepSwitch(timeout);
            return 0;
        }
        // --------------------------------

        if (nonblocking_check) {
            // 执行一次非阻塞的poll, 检测异常或无效fd.
            int res = poll_f(fds, nfds, 0);
            if (res != 0) {
                DebugPrint(dbg_hook, "poll returns %d immediately.", res);
                return res;
            }
        }

        // create io-sentry
        IoSentryPtr io_sentry = MakeShared<IoSentry>(tk, fds, nfds);

        // add file descriptor into epoll or poll.
        bool added = false;
        bool triggered = false;
        for (nfds_t i = 0; i < nfds; ++i) {
            fds[i].revents = 0;     // clear revents
            pollfd & pfd = io_sentry->watch_fds_[i];
            if (pfd.fd < 0)
                continue;

            FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(pfd.fd);
            if (!fd_ctx || fd_ctx->closed()) {
                // bad file descriptor
                pfd.revents = POLLNVAL;
                continue;
            }

            auto result = fd_ctx->add_into_reactor(pfd.events, io_sentry);
            if (result == add_into_reactor_result::failed) {
                // TODO: 兼容文件fd
                pfd.revents = POLLNVAL;
                continue;
            } else if (result == add_into_reactor_result::complete) {
                triggered = true;
            }

            added = true;
        }

        if (!added) {
            errno = 0;
            return nfds;
        }

        if (!triggered) {
            // 没有立即触发, 协程切换至等待状态, 等待被epoll唤醒或超时
            // set timer
            if (timeout > 0)
                io_sentry->timer_ = g_Scheduler.ExpireAt(
                        std::chrono::milliseconds(timeout),
                        [io_sentry]{
                        g_Scheduler.GetIoWait().IOBlockTriggered(io_sentry);
                        });

            // save io-sentry
            tk->io_sentry_ = io_sentry;

            // yield
            g_Scheduler.GetIoWait().CoSwitch();

            // clear task->io_sentry_ reference count
            tk->io_sentry_.reset();

            if (io_sentry->timer_) {
                g_Scheduler.CancelTimer(io_sentry->timer_);
                io_sentry->timer_.reset();
            }
        } else {
            DebugPrint(dbg_hook, "poll triggered immediately.");
        }

        int n = 0;
        for (nfds_t i = 0; i < nfds; ++i) {
            fds[i].revents = io_sentry->watch_fds_[i].revents;
            if (fds[i].revents) ++n;
        }
        errno = 0;
        return n;
    }


#if WITH_CARES

    void buffer_to_hostent(char *buf, hostent* h)
    {
        assert(h);
        assert(buf);

        // h_name
        h->h_name = buf;
        buf += strlen(h->h_name) + 1;

        // h_addrtype & h_length
        h->h_addrtype = *(int*)buf;
        buf += sizeof(int);
        h->h_length = *(int*)buf;
        buf += sizeof(int);

        // h_aliases
        h->h_aliases = (char**)buf;
        char **p = h->h_aliases;
        while (*p)
            ++p;

        h->h_addr_list = ++p;
    }
    bool hostent_dup(hostent * src, hostent * dst, char *buf, size_t & buflen)
    {
        assert(src);
        assert(dst);
        assert(buf);
        assert(buflen);

        size_t expect_length = 0;
        expect_length += strlen(src->h_name) + 1;

        expect_length += sizeof(int) * 2;

        size_t alias_c = 0;
        for (char **p = src->h_aliases; *p; ++p, ++alias_c) {
            expect_length += strlen(*p) + 1;
        }
        expect_length += sizeof(char*) * (alias_c + 1);

        size_t addr_c = 0;
        for (char **p = src->h_addr_list; *p; ++p, ++addr_c) {
            expect_length += strlen(*p) + 1;
        }
        expect_length += sizeof(char*) * (addr_c + 1);
        if (buflen < expect_length) {
            buflen = expect_length;
            return false;
        }

        char* ori_buf = buf;

        // h_name
        strcpy(buf, src->h_name);
        dst->h_name = buf;
        buf += strlen(dst->h_name) + 1;

        // h_addrtype
        *(int*)buf = src->h_addrtype;
        buf += sizeof(int);

        // h_length
        *(int*)buf = src->h_length;
        buf += sizeof(int);

        // h_aliases & h_addr_list
        char *aliases_array = buf;
        buf += sizeof(char*) * (alias_c + 1);

        char *addr_array = buf;
        buf += sizeof(char*) * (addr_c + 1);

        char *alias_buf = buf;
        for (char **p = src->h_aliases; *p; ++p) {
            *(char**)aliases_array = alias_buf;
            aliases_array += sizeof(char*);
            strcpy(alias_buf, *p);
            alias_buf += strlen(*p) + 1;
        }
        *(char**)aliases_array = nullptr;
        buf = alias_buf;

        char *addr_buf = buf;
        for (char **p = src->h_addr_list; *p; ++p) {
            *(char**)addr_array = addr_buf;
            addr_array += sizeof(char*);
            strcpy(addr_buf, *p);
            addr_buf += strlen(*p) + 1;
        }
        *(char**)addr_array = nullptr;
        buf = addr_buf;

        assert(buf - ori_buf == (int)expect_length);
        buffer_to_hostent(ori_buf, dst);
        return true;
    }
    struct cares_async_context
    {
        bool called;
        int *errnop;
        int ret;
        hostent **result;
        char* buf;
        size_t *buflen;
    };
    int gethostbyname_with_ares(const char *name, int af, struct hostent *ret_h,
            char *buf, size_t &buflen, struct hostent **result, int *h_errnop)
    {
        int err = 0;
        if (!h_errnop) h_errnop = &err;

        cares_async_context ctx;
        ctx.called = false;
        ctx.errnop = h_errnop;
        ctx.ret = 0;
        ctx.result = result;
        ctx.buf = buf;
        ctx.buflen = &buflen;

        ares_channel c;
        ares_init(&c);
        ares_gethostbyname(c, name, af,
                [](void * arg, int status, int timeouts, struct hostent *hostent)
                {
                cares_async_context *ctx = (cares_async_context *)arg;
                ctx->called = true;
                if (status != ARES_SUCCESS) {
                *ctx->result = nullptr;

                switch (status) {
                case ARES_ENODATA:
                *ctx->errnop = NO_DATA;
                break;

                case ARES_EBADNAME:
                *ctx->errnop = NO_RECOVERY;
                break;

                case ARES_ENOTFOUND:
                default:
                *ctx->errnop = HOST_NOT_FOUND;
                break;
                }

                ctx->ret = -1;
                return ;
                }

                if (!hostent_dup(hostent, *ctx->result, ctx->buf, *ctx->buflen)) {
                    *ctx->result = nullptr;
                    *ctx->errnop = ENOEXEC;    // 奇怪的错误码.
                    ctx->ret = -1;
                    return ;
                }

                *ctx->errnop = 0;
                ctx->ret = 0;
                }, &ctx);

        fd_set readers, writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);

        int nfds = ares_fds(c, &readers, &writers);
        if (nfds) {
            struct timeval tv, *tvp;
            tvp = ares_timeout(c, NULL, &tv);
            int count = select(nfds, &readers, &writers, NULL, tvp);
            if (count > 0) {
                ares_process(c, &readers, &writers);
            }
        }

        if (ctx.called)
            return ctx.ret;

        // No yet invoke callback.
        *result = nullptr;
        *h_errnop = HOST_NOT_FOUND; // TODO
        return -1;
    }

    int gethostbyaddr_with_ares(const void *addr, int addrlen, int af,
            struct hostent *ret_h, char *buf, size_t &buflen,
            struct hostent **result, int *h_errnop)
    {
        int err = 0;
        if (!h_errnop) h_errnop = &err;

        cares_async_context ctx;
        ctx.called = false;
        ctx.errnop = h_errnop;
        ctx.ret = 0;
        ctx.result = result;
        ctx.buf = buf;
        ctx.buflen = &buflen;

        ares_channel c;
        ares_init(&c);
        ares_gethostbyaddr(c, addr, addrlen, af,
                [](void * arg, int status, int timeouts, struct hostent *hostent)
                {
                cares_async_context *ctx = (cares_async_context *)arg;
                ctx->called = true;
                if (status != ARES_SUCCESS) {
                *ctx->result = nullptr;

                switch (status) {
                case ARES_ENODATA:
                *ctx->errnop = NO_DATA;
                break;

                case ARES_EBADNAME:
                *ctx->errnop = NO_RECOVERY;
                break;

                case ARES_ENOTFOUND:
                default:
                *ctx->errnop = HOST_NOT_FOUND;
                break;
                }

                ctx->ret = -1;
                return ;
                }

                if (!hostent_dup(hostent, *ctx->result, ctx->buf, *ctx->buflen)) {
                    *ctx->result = nullptr;
                    *ctx->errnop = ENOEXEC;    // 奇怪的错误码.
                    ctx->ret = -1;
                    return ;
                }

                *ctx->errnop = 0;
                ctx->ret = 0;
                }, &ctx);

        fd_set readers, writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);

        int nfds = ares_fds(c, &readers, &writers);
        if (nfds) {
            struct timeval tv, *tvp;
            tvp = ares_timeout(c, NULL, &tv);
            int count = select(nfds, &readers, &writers, NULL, tvp);
            if (count > 0) {
                ares_process(c, &readers, &writers);
            }
        }

        if (ctx.called)
            return ctx.ret;

        // No yet invoke callback.
        *result = nullptr;
        *h_errnop = HOST_NOT_FOUND; // TODO
        return -1;
    }
#endif //WITH_CARES

} //namespace co



template <typename OriginF, typename ... Args>
static ssize_t read_write_mode(int fd, OriginF fn, const char* hook_fn_name,
        uint32_t event, int timeout_so, ssize_t buflen, Args && ... args)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook %s. %s coroutine.",
            tk ? tk->DebugInfo() : "nil", hook_fn_name, g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return fn(fd, std::forward<Args>(args)...);

    FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
    if (!fd_ctx || fd_ctx->closed()) {
        errno = EBADF;  // 已被close或无效的fd
        return -1;
    }

    if (!fd_ctx->is_socket())   // 非socket, 暂不HOOK. 以保障文件fd读写正常
        return fn(fd, std::forward<Args>(args)...);

    if (fd_ctx->user_nonblock())
        return fn(fd, std::forward<Args>(args)...);

    timeval tv;
    fd_ctx->get_time_o(timeout_so, &tv);
    int timeout_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    std::chrono::steady_clock::time_point start;
    if (timeout_ms)
        start = std::chrono::steady_clock::now();

retry:
    if ((event == POLLIN && !fd_ctx->readable()) ||
           (event == POLLOUT && !fd_ctx->writable()))
    {
        DebugPrint(dbg_hook, "%s %d unreadable or unwritable. will call poll",
                hook_fn_name, fd);

        int poll_timeout = 0;
        if (!timeout_ms)
            poll_timeout = -1;
        else {
            int expired = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
            if (expired > timeout_ms) {
                errno = EAGAIN;
                return -1;  // 已超时
            }

            // 剩余的等待时间
            poll_timeout = timeout_ms - expired;
        }

        pollfd pfd;
        pfd.fd = fd;
        pfd.events = event;
        pfd.revents = 0;
eintr:
        int triggers = libgo_poll(&pfd, 1, poll_timeout, false);
        if (-1 == triggers) {
            if (errno == EINTR) goto eintr;
            return -1;
        } else if (0 == triggers) {  // poll等待超时
            errno = EAGAIN;
            return -1;
        }

        if (triggers == 1) {
            if (event == POLLIN && (pfd.revents & POLLIN)) {
                DebugPrint(dbg_hook, "poll fd=%d return 1. set readable.", fd);
                fd_ctx->set_readable(true);
            }

            if (event == POLLOUT && (pfd.revents & POLLOUT)) {
                DebugPrint(dbg_hook, "poll fd=%d return 1. set writable.", fd);
                fd_ctx->set_writable(true);
            }
        }
    }

retry_intr_fn:
    ssize_t n = fn(fd, std::forward<Args>(args)...);
    if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 读空了
            if (event == POLLIN) {
                DebugPrint(dbg_hook, "read %d EAGAIN, buflen=%ld.",
                        fd, buflen);
                fd_ctx->set_readable(false);
            } else {
                DebugPrint(dbg_hook, "write %d returns EAGAIN, buflen=%ld.",
                        fd, buflen);
                fd_ctx->set_writable(false);
            }
            goto retry;     // 事件触发 OR epoll惊群效应
        }
        else if (errno == EINTR)
            goto retry_intr_fn;
    }

    if (n >= 0 && n < buflen) {
        // 读空了
        if (event == POLLIN) {
            DebugPrint(dbg_hook, "read %d returns %ld, buflen=%ld.",
                    fd, n, buflen);
            fd_ctx->set_readable(false);
        } else {
            DebugPrint(dbg_hook, "write %d returns %ld, buflen=%ld.",
                    fd, n, buflen);
            fd_ctx->set_writable(false);
        }
    }

    return n;
}

extern "C" {

connect_t connect_f = NULL;
read_t read_f = NULL;
readv_t readv_f = NULL;
recv_t recv_f = NULL;
recvfrom_t recvfrom_f = NULL;
recvmsg_t recvmsg_f = NULL;
write_t write_f = NULL;
writev_t writev_f = NULL;
send_t send_f = NULL;
sendto_t sendto_f = NULL;
sendmsg_t sendmsg_f = NULL;
poll_t poll_f = NULL;
select_t select_f = NULL;
accept_t accept_f = NULL;
sleep_t sleep_f = NULL;
usleep_t usleep_f = NULL;
nanosleep_t nanosleep_f = NULL;
close_t close_f = NULL;
fcntl_t fcntl_f = NULL;
ioctl_t ioctl_f = NULL;
getsockopt_t getsockopt_f = NULL;
setsockopt_t setsockopt_f = NULL;
dup_t dup_f = NULL;
dup2_t dup2_f = NULL;
dup3_t dup3_f = NULL;

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook connect. %s coroutine.",
            tk ? tk->DebugInfo() : "nil", g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return connect_f(fd, addr, addrlen);

    FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
    if (!fd_ctx || fd_ctx->closed()) {
        errno = EBADF;
        return -1;
    }

    if (fd_ctx->user_nonblock())
        return connect_f(fd, addr, addrlen);

    int n = connect_f(fd, addr, addrlen);
    if (n == 0) {
        DebugPrint(dbg_hook, "continue task(%s) connect completed immediately. fd=%d",
                g_Scheduler.GetCurrentTaskDebugInfo(), fd);
        return 0;
    } else if (n != -1 || errno != EINPROGRESS) {
        return n;
    }

    // EINPROGRESS. use poll for wait connect complete.
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    int poll_res = poll(&pfd, 1, s_connect_timeout);
    if (poll_res <= 0 || pfd.revents != POLLOUT) {
        errno = ETIMEDOUT;
        return -1;
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len))
        return -1;

    if (!error)
        return 0;
    else {
        errno = error;
        return -1;
    }
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!accept_f) coroutine_hook_init();
    return read_write_mode(sockfd, accept_f, "accept", POLLIN, SO_RCVTIMEO, 0, addr, addrlen);
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (!read_f) coroutine_hook_init();
    return read_write_mode(fd, read_f, "read", POLLIN, SO_RCVTIMEO, count, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    if (!readv_f) coroutine_hook_init();
    size_t buflen = 0;
    for (int i = 0; i < iovcnt; ++i)
        buflen += iov[i].iov_len;
    return read_write_mode(fd, readv_f, "readv", POLLIN, SO_RCVTIMEO, buflen, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    if (!recv_f) coroutine_hook_init();
    return read_write_mode(sockfd, recv_f, "recv", POLLIN, SO_RCVTIMEO, len, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen)
{
    if (!recvfrom_f) coroutine_hook_init();
    return read_write_mode(sockfd, recvfrom_f, "recvfrom", POLLIN, SO_RCVTIMEO, len, buf, len, flags,
            src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    if (!recvmsg_f) coroutine_hook_init();
    size_t buflen = 0;
    for (size_t i = 0; i < msg->msg_iovlen; ++i)
        buflen += msg->msg_iov[i].iov_len;
    return read_write_mode(sockfd, recvmsg_f, "recvmsg", POLLIN, SO_RCVTIMEO, buflen, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (!write_f) coroutine_hook_init();
    return read_write_mode(fd, write_f, "write", POLLOUT, SO_SNDTIMEO, count, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!writev_f) coroutine_hook_init();
    size_t buflen = 0;
    for (int i = 0; i < iovcnt; ++i)
        buflen += iov[i].iov_len;
    return read_write_mode(fd, writev_f, "writev", POLLOUT, SO_SNDTIMEO, buflen, iov, iovcnt);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    if (!send_f) coroutine_hook_init();
    return read_write_mode(sockfd, send_f, "send", POLLOUT, SO_SNDTIMEO, len, buf, len, flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (!sendto_f) coroutine_hook_init();
    return read_write_mode(sockfd, sendto_f, "sendto", POLLOUT, SO_SNDTIMEO, len, buf, len, flags,
            dest_addr, addrlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    if (!sendmsg_f) coroutine_hook_init();
    size_t buflen = 0;
    for (size_t i = 0; i < msg->msg_iovlen; ++i)
        buflen += msg->msg_iov[i].iov_len;
    return read_write_mode(sockfd, sendmsg_f, "sendmsg", POLLOUT, SO_SNDTIMEO, buflen, msg, flags);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return libgo_poll(fds, nfds, timeout, true);
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, struct timeval *timeout)
{
    if (!select_f) coroutine_hook_init();

    int timeout_ms = -1;
    if (timeout)
        timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook select(nfds=%d, rd_set=%p, wr_set=%p, er_set=%p, timeout=%d ms).",
            tk ? tk->DebugInfo() : "nil",
            (int)nfds, readfds, writefds, exceptfds, timeout_ms);

    if (!tk)
        return select_f(nfds, readfds, writefds, exceptfds, timeout);

    if (timeout_ms == 0)
        return select_f(nfds, readfds, writefds, exceptfds, timeout);

    if (!nfds) {
        g_Scheduler.SleepSwitch(timeout_ms);
        return 0;
    }

    nfds = std::min<int>(nfds, FD_SETSIZE);

    // 执行一次非阻塞的select, 检测异常或无效fd.
    fd_set rfs, wfs, efs;
    FD_ZERO(&rfs);
    FD_ZERO(&wfs);
    FD_ZERO(&efs);
    if (readfds) rfs = *readfds;
    if (writefds) wfs = *writefds;
    if (exceptfds) efs = *exceptfds;
    timeval zero_tv = {0, 0};
    int n = select_f(nfds, (readfds ? &rfs : nullptr),
            (writefds ? &wfs : nullptr),
            (exceptfds ? &efs : nullptr), &zero_tv);
    if (n != 0) {
        if (readfds) *readfds = rfs;
        if (writefds) *writefds = wfs;
        if (exceptfds) *exceptfds = efs;
        return n;
    }

    // -------------------------------------
    // convert fd_set to pollfd, and clear 3 fd_set.
    std::pair<fd_set*, uint32_t> sets[3] = {
        {readfds, POLLIN},
        {writefds, POLLOUT},
        {exceptfds, 0}
    };
    //static const char* set_names[] = {"readfds", "writefds", "exceptfds"};

    std::map<int, int> pfd_map;
    for (int i = 0; i < 3; ++i) {
        fd_set* fds = sets[i].first;
        if (!fds) continue;
        int event = sets[i].second;
        for (int fd = 0; fd < nfds; ++fd) {
            if (FD_ISSET(fd, fds)) {
                pfd_map[fd] |= event;
            }
        }
        FD_ZERO(fds);
    }

    std::vector<pollfd> pfds(pfd_map.size());
    int i = 0;
    for (auto &kv : pfd_map) {
        pollfd &pfd = pfds[i++];
        pfd.fd = kv.first;
        pfd.events = kv.second;
    }
    // -------------------------------------

    // -------------------------------------
    // poll
    n = poll(pfds.data(), pfds.size(), timeout_ms);
    if (n <= 0)
        return n;
    // -------------------------------------

    // -------------------------------------
    // convert pollfd to fd_set.
    int ret = 0;
    for (size_t i = 0; i < pfds.size(); ++i) {
        pollfd &pfd = pfds[i];
        if (pfd.events & POLLIN) {
            if (readfds) {
                FD_SET(pfd.fd, readfds);
                ++ret;
            }
        }

        if (pfd.events & POLLOUT) {
            if (writefds) {
                FD_SET(pfd.fd, writefds);
                ++ret;
            }
        }

        if (pfd.events & ~(POLLIN | POLLOUT)) {
            if (exceptfds) {
                FD_SET(pfd.fd, exceptfds);
                ++ret;
            }
        }
    }
    // -------------------------------------

    return ret;
}

unsigned int sleep(unsigned int seconds)
{
    if (!sleep_f) coroutine_hook_init();

    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook sleep(seconds=%u). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", seconds,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return sleep_f(seconds);

    int timeout_ms = seconds * 1000;
    g_Scheduler.SleepSwitch(timeout_ms);
    return 0;
}

int usleep(useconds_t usec)
{
    if (!usleep_f) coroutine_hook_init();

    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook usleep(microseconds=%u). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", usec,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return usleep_f(usec);

    int timeout_ms = usec / 1000;
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

    if (!tk)
        return nanosleep_f(req, rem);

    g_Scheduler.SleepSwitch(timeout_ms);
    return 0;
}

int close(int fd)
{
    if (!close_f) coroutine_hook_init();
    return FdManager::getInstance().close(fd);
}

int fcntl(int __fd, int __cmd, ...)
{
    if (!fcntl_f) coroutine_hook_init();

    va_list va;
    va_start(va, __cmd);

    switch (__cmd) {
        // int
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            {
                // TODO: support FD_CLOEXEC
                int fd = va_arg(va, int);
                va_end(va);
                int newfd = fcntl_f(__fd, __cmd, fd);
                if (newfd < 0) return newfd;

                if (!FdManager::getInstance().dup(__fd, newfd)) {
                    fprintf(stderr, "fcntl(F_DUPFD) warnning. newfd(%d) is exists in libgo-FdManager. oldfd(%d)\n",
                            newfd, __fd);
                }

                return newfd;
            }

        // int
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#if defined(F_SETPIPE_SZ)
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }

        // int
        case F_SETFL:
            {
                int flags = va_arg(va, int);
                va_end(va);
                FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(__fd);
                if (!fd_ctx || fd_ctx->closed()) return fcntl_f(__fd, __cmd, flags);
                if (!fd_ctx->is_socket()) return fcntl_f(__fd, __cmd, flags);
                fd_ctx->set_user_nonblock(flags & O_NONBLOCK);
                if (fd_ctx->sys_nonblock())
                    flags |= O_NONBLOCK;
                else
                    flags &= ~O_NONBLOCK;
                return fcntl_f(__fd, __cmd, flags);
            }

        // struct flock*
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }

        // struct f_owner_ex*
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }

        // void
        case F_GETFL:
            {
                va_end(va);
                int flags = fcntl_f(__fd, __cmd);
                FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(__fd);
                if (!fd_ctx || fd_ctx->closed()) return flags;
                if (!fd_ctx->is_socket()) return flags;
                if (fd_ctx->user_nonblock())
                    return flags | O_NONBLOCK;
                else
                    return flags & ~O_NONBLOCK;
            }

        // void
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#if defined(F_GETPIPE_SZ)
        case F_GETPIPE_SZ:
#endif
        default:
            {
                va_end(va);
                return fcntl_f(__fd, __cmd);
            }
    }
}

int ioctl(int fd, unsigned long int request, ...)
{
    if (!ioctl_f) coroutine_hook_init();

    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if (FIONBIO == request) {
        bool user_nonblock = !!*(int*)arg;
        FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(fd);
        if (!fd_ctx || fd_ctx->closed()) return ioctl_f(fd, request, arg);
        if (!fd_ctx->is_socket()) return ioctl_f(fd, request, arg);

        fd_ctx->set_user_nonblock(user_nonblock);
        return 0;
    }

    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!getsockopt_f) coroutine_hook_init();
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!setsockopt_f) coroutine_hook_init();

    if (level == SOL_SOCKET) {
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            FdCtxPtr fd_ctx = FdManager::getInstance().get_fd_ctx(sockfd);
            if (fd_ctx) {
                fd_ctx->set_time_o(optname, *(const timeval*)optval);
            }
        }
    }

    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

int dup(int oldfd)
{
    if (!dup_f) coroutine_hook_init();

    int newfd = dup_f(oldfd);
    if (newfd < 0) return newfd;

    if (!FdManager::getInstance().dup(oldfd, newfd)) {
        fprintf(stderr, "dup warnning. newfd(%d) is exists in libgo-FdManager. oldfd(%d)\n",
                newfd, oldfd);
    }

    return newfd;
}
// TODO: support FD_CLOEXEC
int dup2(int oldfd, int newfd)
{
    if (!dup2_f) coroutine_hook_init();

    if (newfd < 0 || oldfd < 0 || oldfd == newfd) return dup2_f(oldfd, newfd);

    int ret = dup2_f(oldfd, newfd);
    if (ret < 0) return ret;

    FdManager::getInstance().close(newfd, false);   // 模拟dup2内部未能hook到的close.

    if (!FdManager::getInstance().dup(oldfd, newfd)) {
        fprintf(stderr, "dup2 warnning. newfd(%d) is exists in libgo-FdManager. oldfd(%d)\n",
                newfd, oldfd);
    }

    return ret;
}
// TODO: support FD_CLOEXEC
int dup3(int oldfd, int newfd, int flags)
{
    if (!dup3_f) coroutine_hook_init();
    if (!dup3_f) {
        errno = EPERM;
        return -1;
    }

    if (newfd < 0 || oldfd < 0 || oldfd == newfd) return dup3_f(oldfd, newfd, flags);

    int ret = dup3_f(oldfd, newfd, flags);
    if (ret < 0) return ret;

    FdManager::getInstance().close(newfd, false);   // 模拟dup2内部未能hook到的close.

    if (!FdManager::getInstance().dup(oldfd, newfd)) {
        fprintf(stderr, "dup3 warnning. newfd(%d) is exists in libgo-FdManager. oldfd(%d)\n",
                newfd, oldfd);
    }

    return ret;
}

#if WITH_CARES
struct hostent* co_gethostbyname2(const char *name, int af)
{
    static size_t s_buflen = 8192;
    static char *s_buffer = (char*)malloc(s_buflen);
    static hostent h;
    hostent *p = &h;

    int err = 0;
    size_t buflen = s_buflen;
    int res = co::gethostbyname_with_ares(name, af, p, s_buffer, buflen,
            &p, &err);
    if (res == -1 && buflen > s_buflen) {
        s_buflen = buflen;
        s_buffer = (char*)realloc(s_buffer, s_buflen);
        res = co::gethostbyname_with_ares(name, af, p, s_buffer, buflen,
                &p, &err);
    }

    if (res == 0) {
        return p;
    }

    h_errno = err;
    return nullptr;
}

struct hostent* gethostbyname(const char *name)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname(%s). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", name ? name : "nil",
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    return co_gethostbyname2(name, AF_INET);
}

struct hostent *gethostbyname2(const char *name, int af)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname2(%s, %d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", name ? name : "nil", af,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    return co_gethostbyname2(name, af);
}

int gethostbyname_r(const char *name, struct hostent *ret_h, char *buf,
        size_t buflen, struct hostent **result, int *h_errnop)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname_r(name=%s, buflen=%d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", name ? name : "nil", (int)buflen,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    return co::gethostbyname_with_ares(name, AF_INET, ret_h, buf, buflen, result, h_errnop);
}

int gethostbyname2_r(const char *name, int af,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname2_r(name=%s, af=%d, buflen=%d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil", name ? name : "nil", af, (int)buflen,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    return co::gethostbyname_with_ares(name, af, ret, buf, buflen, result, h_errnop);
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyaddr_r(ip=%s, type=%d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil",
            inet_ntoa(*(in_addr*)addr), type,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    static size_t s_buflen = 8192;
    static char *s_buffer = (char*)malloc(s_buflen);
    static hostent h;
    hostent *p = &h;

    int err = 0;
    size_t buflen = s_buflen;
    int res = co::gethostbyaddr_with_ares(addr, len, type, p, s_buffer, buflen,
            &p, &err);
    if (res == -1 && buflen > s_buflen) {
        s_buflen = buflen;
        s_buffer = (char*)realloc(s_buffer, s_buflen);
        res = co::gethostbyaddr_with_ares(addr, len, type, p, s_buffer, buflen,
                &p, &err);
    }

    if (res == 0) {
        return p;
    }

    h_errno = err;
    return nullptr;
}

int gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyaddr_r(ip=%s, type=%d, buflen=%d). %s coroutine.",
            tk ? tk->DebugInfo() : "nil",
            inet_ntoa(*(in_addr*)addr), type, (int)buflen,
            g_Scheduler.IsCoroutine() ? "In" : "Not in");

    return co::gethostbyaddr_with_ares(addr, len, type, ret, buf, buflen, result, h_errnop);
}

#endif


// safe signal
#if WITH_SAFE_SIGNAL
sighandler_t signal(int signum, sighandler_t handler)
{
    return co::HookSignal::getInstance().SignalSyscall(signum, handler);
}
#endif

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
extern int __close(int);
extern int __fcntl(int __fd, int __cmd, ...);
extern int __ioctl(int fd, unsigned long int request, ...);
extern int __getsockopt(int sockfd, int level, int optname,
        void *optval, socklen_t *optlen);
extern int __setsockopt(int sockfd, int level, int optname,
        const void *optval, socklen_t optlen);
extern int __dup(int);
extern int __dup2(int, int);
extern int __dup3(int, int, int);
extern int __usleep(useconds_t usec);

// 某些版本libc.a中没有__usleep.
__attribute__((weak))
int __usleep(useconds_t usec)
{
    timespec req = {usec / 1000000, usec * 1000};
    return __nanosleep(&req, nullptr);
}

#endif
} // extern "C"

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
    usleep_f = (usleep_t)dlsym(RTLD_NEXT, "usleep");
    nanosleep_f = (nanosleep_t)dlsym(RTLD_NEXT, "nanosleep");
    close_f = (close_t)dlsym(RTLD_NEXT, "close");
    fcntl_f = (fcntl_t)dlsym(RTLD_NEXT, "fcntl");
    ioctl_f = (ioctl_t)dlsym(RTLD_NEXT, "ioctl");
    getsockopt_f = (getsockopt_t)dlsym(RTLD_NEXT, "getsockopt");
    setsockopt_f = (setsockopt_t)dlsym(RTLD_NEXT, "setsockopt");
    dup_f = (dup_t)dlsym(RTLD_NEXT, "dup");
    dup2_f = (dup2_t)dlsym(RTLD_NEXT, "dup2");
    dup3_f = (dup3_t)dlsym(RTLD_NEXT, "dup3");
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
    usleep_f = &__usleep;
    nanosleep_f = &__nanosleep;
    close_f = &__close;
    fcntl_f = &__fcntl;
    ioctl_f = &__ioctl;
    getsockopt_f = &__getsockopt;
    setsockopt_f = &__setsockopt;
    dup_f = &__dup;
    dup2_f = &__dup2;
    dup3_f = &__dup3;
#endif

    if (!connect_f || !read_f || !write_f || !readv_f || !writev_f || !send_f
            || !sendto_f || !sendmsg_f || !accept_f || !poll_f || !select_f
            || !sleep_f|| !usleep_f || !nanosleep_f || !close_f || !fcntl_f || !setsockopt_f
            || !getsockopt_f || !dup_f || !dup2_f 
            // 老版本linux中没有dup3, 无需校验
            // || !dup3_f
            )
    {
        fprintf(stderr, "Hook syscall failed. Please don't remove libc.a when static-link.\n");
        exit(1);
    }

    coroutine_hook_inited = true;
}

} //namespace co
