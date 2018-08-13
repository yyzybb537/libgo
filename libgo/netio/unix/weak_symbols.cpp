#include <unistd.h>
#include <resolv.h>
#include <netdb.h>
#include <poll.h>
#include "assert.h"

extern "C" {

__attribute__((weak))
int __pipe(int pipefd[2])
{
    assert(false);
    return -1;
}
__attribute__((weak))
int __pipe2(int pipefd[2], int flags)
{
    assert(false);
    return -1;
}
__attribute__((weak))
int __socket(int domain, int type, int protocol)
{
    assert(false);
    return -1;
}
__attribute__((weak))
int __socketpair(int domain, int type, int protocol, int sv[2])
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __read(int fd, void *buf, size_t count)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __readv(int fd, const struct iovec *iov, int iovcnt)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __recv(int sockfd, void *buf, size_t len, int flags)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __recvfrom(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __write(int fd, const void *buf, size_t count)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __writev(int fd, const struct iovec *iov, int iovcnt)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __send(int sockfd, const void *buf, size_t len, int flags)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __sendto(int sockfd, const void *buf, size_t len, int flags,
        const struct sockaddr *dest_addr, socklen_t addrlen)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 ssize_t __sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __libc_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __libc_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __select(int nfds, fd_set *readfds, fd_set *writefds,
        fd_set *exceptfds, struct timeval *timeout)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 unsigned int __sleep(unsigned int seconds)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __nanosleep(const struct timespec *req, struct timespec *rem)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __close(int)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __fcntl(int __fd, int __cmd, ...)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __ioctl(int fd, unsigned long int request, ...)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __getsockopt(int sockfd, int level, int optname,
        void *optval, socklen_t *optlen)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __setsockopt(int sockfd, int level, int optname,
        const void *optval, socklen_t optlen)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __dup(int)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __dup2(int, int)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __dup3(int, int, int)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __usleep(useconds_t usec)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __new_fclose(FILE *fp)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __gethostbyname_r(const char *__restrict __name,
        struct hostent *__restrict __result_buf,
        char *__restrict __buf, size_t __buflen,
        struct hostent **__restrict __result,
        int *__restrict __h_errnop)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __gethostbyname2_r(const char *name, int af,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    assert(false);
    return -1;
}
__attribute__((weak))
 int __gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    assert(false);
    return -1;
}
__attribute__((weak))
int __epoll_wait_nocancel(int epfd, struct epoll_event *events,
        int maxevents, int timeout)
{
    assert(false);
    return -1;
}

} // extern "C"

namespace co {
    void enableWeakSymbols() {}
} // namespace co
