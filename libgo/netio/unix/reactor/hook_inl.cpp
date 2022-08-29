#include "../hook.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/select.h>
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
#include "../../../scheduler/processer.h"
#include "reactor.h"
#include "hook_helper.h"
#include "../../../sync/co_mutex.h"
#include "../../../cls/co_local_storage.h"
#if defined(LIBGO_SYS_Linux)
# include <sys/epoll.h>
#elif defined(LIBGO_SYS_FreeBSD)
# include <sys/event.h>
# include <sys/time.h>
#endif
using namespace co;

// 设置阻塞式connect超时时间(-1无限时)
static thread_local CoMutex g_dns_mtx;

namespace co {
    void initHook();
    extern int doInitHook();

    bool setTcpConnectTimeout(int fd, int milliseconds)
    {
        FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
        if (!ctx) return false;

        ctx->SetTcpConnectTimeout(milliseconds);
        return true;
    }

#if defined(LIBGO_SYS_Linux)
    int libgo_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
    {
        Task* tk = Processer::GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) call libgo_epoll_wait. %s coroutine.",
                tk->DebugInfo(), Processer::IsCoroutine() ? "In" : "Not in");

        if (!tk)
            return epoll_wait_f(epfd, events, maxevents, timeout);

        int res = epoll_wait_f(epfd, events, maxevents, 0);
        if (res == -1) {
            return -1;
        } else if (res > 0) {
            return res;
        }

        // res == 0, wait by poll.
        struct pollfd pfd;
        pfd.fd = epfd;
        pfd.events = POLLIN | POLLOUT;
        pfd.revents = 0;
        res = poll(&pfd, 1, timeout);
        if (res == -1)
            return -1;

        return epoll_wait_f(epfd, events, maxevents, 0);
    }
#elif defined(LIBGO_SYS_FreeBSD)
#endif

    inline int libgo_poll(struct pollfd *fds, nfds_t nfds, int timeout, bool nonblocking_check)
    {
        Task* tk = Processer::GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) hook libgo_poll(first-fd=%d, nfds=%d, timeout=%d, nonblocking=%d). %s coroutine.",
                tk->DebugInfo(),
                nfds > 0 ? fds[0].fd : -1,
                (int)nfds, timeout, nonblocking_check,
                Processer::IsCoroutine() ? "In" : "Not in");

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
            if (timeout > 0) {
                Processer::Suspend(std::chrono::milliseconds(timeout));
                Processer::StaticCoYield();
            }
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

        short int *arrRevents = new short int[nfds];
        memset(arrRevents, 0, sizeof(short int) * nfds);
        std::shared_ptr<short int> revents(arrRevents, [](short int* p){ delete[] p; });

        Processer::SuspendEntry entry;
        if (timeout > 0)
            entry = Processer::Suspend(std::chrono::milliseconds(timeout));
        else
            entry = Processer::Suspend();

        // add file descriptor into epoll or poll.
        bool added = false;
        for (nfds_t i = 0; i < nfds; ++i) {
            pollfd & pfd = fds[i];
            pfd.revents = 0;     // clear revents
            if (pfd.fd < 0)
                continue;

            if (!Reactor::Select(pfd.fd).Add(pfd.fd, pfd.events, Reactor::Entry(entry, revents, i))) {
                // bad file descriptor
                arrRevents[i] = POLLNVAL;
                continue;
            }

            added = true;
        }

        if (!added) {
            // 全部fd都无法加入epoll
            for (nfds_t i = 0; i < nfds; ++i)
                fds[i].revents = arrRevents[i];
            errno = 0;
            Processer::Wakeup(entry);
            Processer::StaticCoYield();
            return nfds;
        }

        Processer::StaticCoYield();

        int n = 0;
        for (nfds_t i = 0; i < nfds; ++i) {
            fds[i].revents = arrRevents[i];
            if (fds[i].revents) ++n;
        }
        errno = 0;
        return n;
    }
} //namespace co

template <typename OriginF, typename ... Args>
static ssize_t read_write_mode(int fd, OriginF fn, const char* hook_fn_name,
        short int event, int timeout_so, ssize_t buflen, Args && ... args)
{
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook %s(fd=%d, buflen=%d). %s coroutine.",
            tk->DebugInfo(), hook_fn_name, fd, (int)buflen,
            Processer::IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return fn(fd, std::forward<Args>(args)...);

    FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);

    if (!ctx || ctx->IsNonBlocking())
        return fn(fd, std::forward<Args>(args)...);

    long socketTimeout = ctx->GetSocketTimeoutMicroSeconds(timeout_so);
    int pollTimeout = (socketTimeout == 0) ? -1 : (socketTimeout < 1000 ? 1 : socketTimeout / 1000);

    struct pollfd fds;
    fds.fd = fd;
    fds.events = event;
    fds.revents = 0;

eintr:
    int triggers = libgo_poll(&fds, 1, pollTimeout, true);
    if (-1 == triggers) {
        if (errno == EINTR) goto eintr;
        return -1;
    } else if (0 == triggers) {  // poll等待超时
        errno = EAGAIN;
        return -1;
    }

retry_intr_fn:
    ssize_t res = fn(fd, std::forward<Args>(args)...);
    if (res == -1) {
        if (errno == EINTR)
            goto retry_intr_fn;
        return -1;
    }

    return res;
}

extern "C" {

int pipe(int pipefd[2])
{
    if (!socket_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook pipe.", tk->DebugInfo());

    int res = pipe_f(pipefd);
    if (res == 0) {
        HookHelper::getInstance().OnCreate(pipefd[0], eFdType::ePipe);
        HookHelper::getInstance().OnCreate(pipefd[1], eFdType::ePipe);
    }
    return res;
}
#if defined(LIBGO_SYS_Linux)
int pipe2(int pipefd[2], int flags)
{
    if (!socket_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook pipe.", tk->DebugInfo());

    int res = pipe2_f(pipefd, flags);
    if (res == 0) {
        HookHelper::getInstance().OnCreate(pipefd[0], eFdType::ePipe, !!(flags & O_NONBLOCK));
        HookHelper::getInstance().OnCreate(pipefd[1], eFdType::ePipe, !!(flags & O_NONBLOCK));
    }
    return res;
}
#endif
int socket(int domain, int type, int protocol)
{
    if (!socket_f) initHook();

    Task* tk = Processer::GetCurrentTask();

    int sock = socket_f(domain, type, protocol);
    if (sock >= 0) {
        HookHelper::getInstance().OnCreate(sock, eFdType::eSocket, false, SocketAttribute(domain, type, protocol));
    }

    DebugPrint(dbg_hook, "task(%s) hook socket, returns %d.", tk->DebugInfo(), sock);
    return sock;
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    if (!socketpair_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook socketpair.", tk->DebugInfo());

    int res = socketpair_f(domain, type, protocol, sv);
    if (res == 0) {
        HookHelper::getInstance().OnCreate(sv[0], eFdType::eSocket, false, SocketAttribute(domain, type, protocol));
        HookHelper::getInstance().OnCreate(sv[1], eFdType::eSocket, false, SocketAttribute(domain, type, protocol));
    }
    return res;
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!connect_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook connect. %s coroutine.",
            tk->DebugInfo(), Processer::IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return connect_f(fd, addr, addrlen);

    FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);

    if (!ctx)
        return connect_f(fd, addr, addrlen);

    if (!ctx->IsTcpSocket() || ctx->IsNonBlocking())
        return connect_f(fd, addr, addrlen);

    int res;

    {
        NonBlockingGuard guard(ctx);
        res = connect_f(fd, addr, addrlen);
    }

    if (res == 0) {
        DebugPrint(dbg_hook, "continue task(%s) connect completed immediately. fd=%d",
                tk->DebugInfo(), fd);
        return 0;
    } else if (res == -1 && errno == EINPROGRESS) {
        // poll wait
    } else {
        return res;
    }

    // EINPROGRESS. use poll for wait connect complete.
    int connectTimeout = ctx->GetTcpConnectTimeout();
    int pollTimeout = (connectTimeout == 0) ? -1 : connectTimeout;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    int triggers = libgo_poll(&pfd, 1, pollTimeout, false);
    if (triggers <= 0 || pfd.revents != POLLOUT) {
        errno = ETIMEDOUT;
        return -1;
    }

    int sockErr = 0;
    socklen_t len = sizeof(int);
    if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &len))
        return -1;

    if (!sockErr)
        return 0;

    errno = sockErr;
    return -1;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!accept_f) initHook();

    FdContextPtr ctx = HookHelper::getInstance().GetFdContext(sockfd);
    if (!ctx) {
        Task* tk = Processer::GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) hook accept(fd=%d) no fd_context.", tk->DebugInfo(), sockfd);
        errno = EBADF;
        return -1;
    }

    int sock = read_write_mode(sockfd, accept_f, "accept", POLLIN, SO_RCVTIMEO, 0, addr, addrlen);
    if (sock >= 0) {
        HookHelper::getInstance().OnCreate(sock, eFdType::eSocket, false, ctx->GetSocketAttribute());
    }
    return sock;
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (!read_f) initHook();
    return read_write_mode(fd, read_f, "read", POLLIN, SO_RCVTIMEO, count, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    if (!readv_f) initHook();
    size_t buflen = 0;
    for (int i = 0; i < iovcnt; ++i)
        buflen += iov[i].iov_len;
    return read_write_mode(fd, readv_f, "readv", POLLIN, SO_RCVTIMEO, buflen, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    if (!recv_f) initHook();
    return read_write_mode(sockfd, recv_f, "recv", POLLIN, SO_RCVTIMEO, len, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen)
{
    if (!recvfrom_f) initHook();
    return read_write_mode(sockfd, recvfrom_f, "recvfrom", POLLIN, SO_RCVTIMEO, len, buf, len, flags,
            src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    if (!recvmsg_f) initHook();
    size_t buflen = 0;
    for (size_t i = 0; i < msg->msg_iovlen; ++i)
        buflen += msg->msg_iov[i].iov_len;
    return read_write_mode(sockfd, recvmsg_f, "recvmsg", POLLIN, SO_RCVTIMEO, buflen, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    if (!write_f) initHook();
    return read_write_mode(fd, write_f, "write", POLLOUT, SO_SNDTIMEO, count, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!writev_f) initHook();
    size_t buflen = 0;
    for (int i = 0; i < iovcnt; ++i)
        buflen += iov[i].iov_len;
    return read_write_mode(fd, writev_f, "writev", POLLOUT, SO_SNDTIMEO, buflen, iov, iovcnt);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    if (!send_f) initHook();
    return read_write_mode(sockfd, send_f, "send", POLLOUT, SO_SNDTIMEO, len, buf, len, flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (!sendto_f) initHook();
    return read_write_mode(sockfd, sendto_f, "sendto", POLLOUT, SO_SNDTIMEO, len, buf, len, flags,
            dest_addr, addrlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    if (!sendmsg_f) initHook();
    size_t buflen = 0;
    for (size_t i = 0; i < msg->msg_iovlen; ++i)
        buflen += msg->msg_iov[i].iov_len;
    return read_write_mode(sockfd, sendmsg_f, "sendmsg", POLLOUT, SO_SNDTIMEO, buflen, msg, flags);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!poll_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook poll(first-fd=%d, nfds=%d, timeout=%d). %s coroutine.",
            tk->DebugInfo(),
            nfds > 0 ? fds[0].fd : -1,
            (int)nfds, timeout,
            Processer::IsCoroutine() ? "In" : "Not in");

    return libgo_poll(fds, nfds, timeout, true);
}

// ---------------------------------------------------------------------------
// ------ for dns syscall
int __poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!poll_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook __poll(first-fd=%d, nfds=%d, timeout=%d). %s coroutine.",
            tk->DebugInfo(),
            nfds > 0 ? fds[0].fd : -1,
            (int)nfds, timeout,
            Processer::IsCoroutine() ? "In" : "Not in");

    return libgo_poll(fds, nfds, timeout, true);
}


#if defined(LIBGO_SYS_Linux)
struct hostent* gethostbyname(const char* name)
{
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname(name=%s).",
            tk->DebugInfo(), name ? name : "");

    if (!name) return nullptr;
    std::vector<char> & buf = CLS(std::vector<char>);
    if (buf.capacity() > 1024) {
        buf.resize(1024);
        buf.shrink_to_fit();
    } else if (buf.size() < 64) {
        buf.resize(64);
    }

    struct hostent & refh = CLS(struct hostent);
    struct hostent * host = &refh;
	struct hostent * result = nullptr;
    int & host_errno = CLS(int);

	int ret = -1;
	while (ret = gethostbyname_r(name, host, &buf[0], 
				buf.size(), &result, &host_errno) == ERANGE && 
				host_errno == NETDB_INTERNAL )
	{
        if (buf.size() < 1024)
            buf.resize(1024);
        else
            buf.resize(buf.size() * 2);
	}

	if (ret == 0 && (host == result)) 
	{
		return host;
	}

    return nullptr;
}
int gethostbyname_r(const char *__restrict name,
			    struct hostent *__restrict __result_buf,
			    char *__restrict __buf, size_t __buflen,
			    struct hostent **__restrict __result,
			    int *__restrict __h_errnop)
{
    if (!gethostbyname_r_f) initHook();
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname_r(name=%s, buflen=%d).",
            tk->DebugInfo(), name ? name : "", (int)__buflen);
    std::unique_lock<CoMutex> lock(g_dns_mtx);
    return gethostbyname_r_f(name, __result_buf, __buf, __buflen, __result, __h_errnop);
}

struct hostent* gethostbyname2(const char* name, int af)
{
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname2(name=%s, af=%d).",
            tk->DebugInfo(), name ? name : "", af);

    if (!name) return nullptr;
    std::vector<char> & buf = CLS(std::vector<char>);
    if (buf.capacity() > 1024) {
        buf.resize(1024);
        buf.shrink_to_fit();
    } else if (buf.size() < 64) {
        buf.resize(64);
    }

    struct hostent & refh = CLS(struct hostent);
    struct hostent * host = &refh;
	struct hostent * result = nullptr;
    int & host_errno = CLS(int);

	int ret = -1;
	while (ret = gethostbyname2_r(name, af, host, &buf[0], 
				buf.size(), &result, &host_errno) == ERANGE && 
				host_errno == NETDB_INTERNAL )
	{
        if (buf.size() < 1024)
            buf.resize(1024);
        else
            buf.resize(buf.size() * 2);
	}

	if (ret == 0 && (host == result)) 
	{
		return host;
	}

    return nullptr;
}
int gethostbyname2_r(const char *name, int af,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    if (!gethostbyname2_r_f) initHook();
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyname2_r(name=%s, af=%d, buflen=%d).",
            tk->DebugInfo(), name ? name : "", af, (int)buflen);
    std::unique_lock<CoMutex> lock(g_dns_mtx);
    return gethostbyname2_r_f(name, af, ret, buf, buflen, result, h_errnop);
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyaddr(type=%d).",
            tk->DebugInfo(), type);

    if (!addr) return nullptr;
    std::vector<char> & buf = CLS(std::vector<char>);
    if (buf.capacity() > 1024) {
        buf.resize(1024);
        buf.shrink_to_fit();
    } else if (buf.size() < 64) {
        buf.resize(64);
    }

    struct hostent & refh = CLS(struct hostent);
    struct hostent * host = &refh;
	struct hostent * result = nullptr;
    int & host_errno = CLS(int);

	int ret = -1;
	while (ret = gethostbyaddr_r(addr, len, type,
                host, &buf[0], buf.size(), &result, &host_errno) == ERANGE && 
				host_errno == NETDB_INTERNAL )
	{
        if (buf.size() < 1024)
            buf.resize(1024);
        else
            buf.resize(buf.size() * 2);
	}

	if (ret == 0 && (host == result))
		return host;

    return nullptr;

}
int gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    if (!gethostbyaddr_r_f) initHook();
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook gethostbyaddr_r(buflen=%d).",
            tk->DebugInfo(), (int)buflen);
    std::unique_lock<CoMutex> lock(g_dns_mtx);
    return gethostbyaddr_r_f(addr, len, type, ret, buf, buflen, result, h_errnop);
}
#endif

// ---------------------------------------------------------------------------

int select(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, struct timeval *timeout)
{
    if (!select_f) initHook();

    int timeout_ms = -1;
    if (timeout)
        timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook select(nfds=%d, rd_set=%p, wr_set=%p, er_set=%p, timeout=%d ms).",
            tk->DebugInfo(),
            (int)nfds, readfds, writefds, exceptfds, timeout_ms);

    if (!tk)
        return select_f(nfds, readfds, writefds, exceptfds, timeout);

    if (timeout_ms == 0)
        return select_f(nfds, readfds, writefds, exceptfds, timeout);

    if (!nfds) {
        Processer::Suspend(std::chrono::milliseconds(timeout_ms));
        Processer::StaticCoYield();
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
    n = libgo_poll(pfds.data(), pfds.size(), timeout_ms, true);
    if (n <= 0)
        return n;
    // -------------------------------------

    // -------------------------------------
    // convert pollfd to fd_set.
    int ret = 0;
    for (size_t i = 0; i < pfds.size(); ++i) {
        pollfd &pfd = pfds[i];
        if (pfd.revents & POLLIN) {
            if (readfds) {
                FD_SET(pfd.fd, readfds);
                ++ret;
            }
        }

        if (pfd.revents & POLLOUT) {
            if (writefds) {
                FD_SET(pfd.fd, writefds);
                ++ret;
            }
        }

        if (pfd.revents & ~(POLLIN | POLLOUT)) {
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
    if (!sleep_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook sleep(seconds=%u). %s coroutine.",
            tk->DebugInfo(), seconds,
            Processer::IsCoroutine() ? "In" : "Not in");

    if (!tk)
        return sleep_f(seconds);

    Processer::Suspend(std::chrono::seconds(seconds));
    Processer::StaticCoYield();
    return 0;
}

int usleep(useconds_t usec)
{
    if (!usleep_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    if (tk) {
        DebugPrint(dbg_hook, "task(%s) hook usleep(microseconds=%u). %s coroutine.",
                tk->DebugInfo(), usec,
                Processer::IsCoroutine() ? "In" : "Not in");
    }

    if (!tk)
        return usleep_f(usec);

    Processer::Suspend(std::chrono::microseconds(usec));
    Processer::StaticCoYield();
    return 0;

}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!nanosleep_f) initHook();

    Task* tk = Processer::GetCurrentTask();
    if (tk) {
        int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
        DebugPrint(dbg_hook, "task(%s) hook nanosleep(milliseconds=%d). %s coroutine.",
                tk->DebugInfo(), timeout_ms,
                Processer::IsCoroutine() ? "In" : "Not in");
    }

    if (!tk)
        return nanosleep_f(req, rem);

    Processer::Suspend(std::chrono::nanoseconds(req->tv_sec * 1000000000 + req->tv_nsec));
    Processer::StaticCoYield();
    return 0;
}

int close(int fd)
{
    if (!close_f) initHook();
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook close(fd=%d).", tk->DebugInfo(), fd);
            
    HookHelper::getInstance().OnClose(fd);
    return close_f(fd);
}

int __close(int fd)
{
    if (!close_f) initHook();
    Task* tk = Processer::GetCurrentTask();
    DebugPrint(dbg_hook, "task(%s) hook __close(fd=%d).", tk->DebugInfo(), fd);
            
    HookHelper::getInstance().OnClose(fd);
    return close_f(fd);
}

int fcntl(int __fd, int __cmd, ...)
{
    if (!fcntl_f) initHook();

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

                HookHelper::getInstance().OnDup(fd, newfd);
                return newfd;
            }

        // int
        case F_SETFD:
        case F_SETOWN:

#if defined(LIBGO_SYS_Linux)
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#endif

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

                FdContextPtr ctx = HookHelper::getInstance().GetFdContext(__fd);
                if (ctx) {
                    bool isNonBlocking = !!(flags & O_NONBLOCK);
                    ctx->OnSetNonBlocking(isNonBlocking);
                }
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
#if defined(LIBGO_SYS_Linux)
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(__fd, __cmd, arg);
            }
#endif

        // void
        case F_GETFL:
            {
                va_end(va);
                return fcntl_f(__fd, __cmd);
            }

        // void
        case F_GETFD:
        case F_GETOWN:

#if defined(LIBGO_SYS_Linux)
        case F_GETSIG:
        case F_GETLEASE:
#endif

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
    if (!ioctl_f) initHook();

    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    if (FIONBIO == request) {
        FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
        if (ctx) {
            bool isNonBlocking = !!*(int*)arg;
            ctx->OnSetNonBlocking(isNonBlocking);
        }
    }

    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!getsockopt_f) initHook();
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!setsockopt_f) initHook();

    int res = setsockopt_f(sockfd, level, optname, optval, optlen);

    if (res == 0 && level == SOL_SOCKET) {
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            FdContextPtr ctx = HookHelper::getInstance().GetFdContext(sockfd);
            if (ctx) {
                const timeval & tv = *(const timeval*)optval;
                int microseconds = tv.tv_sec * 1000000 + tv.tv_usec;
                ctx->OnSetSocketTimeout(optname, microseconds);
            }
        }
    }

    return res;
}

int dup(int oldfd)
{
    if (!dup_f) initHook();

    int newfd = dup_f(oldfd);
    if (newfd < 0) return newfd;

    HookHelper::getInstance().OnDup(oldfd, newfd);
    return newfd;
}
// TODO: support FD_CLOEXEC
int dup2(int oldfd, int newfd)
{
    if (!dup2_f) initHook();

    if (newfd < 0 || oldfd < 0 || oldfd == newfd) return dup2_f(oldfd, newfd);

    int ret = dup2_f(oldfd, newfd);
    if (ret < 0) return ret;

    HookHelper::getInstance().OnDup(oldfd, newfd);
    return ret;
}
// TODO: support FD_CLOEXEC
int dup3(int oldfd, int newfd, int flags)
{
    if (!dup3_f) initHook();
    if (!dup3_f) {
        errno = EPERM;
        return -1;
    }

    if (newfd < 0 || oldfd < 0 || oldfd == newfd) return dup3_f(oldfd, newfd, flags);

    int ret = dup3_f(oldfd, newfd, flags);
    if (ret < 0) return ret;

    HookHelper::getInstance().OnDup(oldfd, newfd);
    return ret;
}

int fclose(FILE* fp)
{
    if (!fclose_f) initHook();
    int fd = fileno(fp);
    HookHelper::getInstance().OnClose(fd);
    return fclose_f(fp);
}

#if defined(LIBGO_SYS_Linux)
/*
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (!epoll_wait_f) initHook();
    return libgo_epoll_wait(epfd, events, maxevents, timeout);
}
*/
#elif defined(LIBGO_SYS_FreeBSD)
#endif

} // extern "C"

namespace co {

void initHook()
{
    static int isInit = doInitHook();
    (void)isInit;
}

} // namespace co
