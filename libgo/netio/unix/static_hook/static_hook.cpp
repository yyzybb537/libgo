#include "../../../common/config.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#if defined(LIBGO_SYS_Linux)
# include <sys/epoll.h>
#elif defined(LIBGO_SYS_FreeBSD)
# include <sys/event.h>
#endif

extern "C" {

extern int __pipe(int pipefd[2]);
extern int __pipe2(int pipefd[2], int flags);
extern int __socket(int domain, int type, int protocol);
extern int __socketpair(int domain, int type, int protocol, int sv[2]);
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
extern int __libc_poll(struct pollfd *fds, nfds_t nfds, int timeout);
extern int __select(int nfds, fd_set *readfds, fd_set *writefds,
                          fd_set *exceptfds, struct timeval *timeout);
extern unsigned int __sleep(unsigned int seconds);
extern int __nanosleep(const struct timespec *req, struct timespec *rem);
extern int __libc_close(int);
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
extern int __new_fclose(FILE *fp);

#if defined(LIBGO_SYS_Linux)
extern int __gethostbyname_r(const char *__restrict __name,
			    struct hostent *__restrict __result_buf,
			    char *__restrict __buf, size_t __buflen,
			    struct hostent **__restrict __result,
			    int *__restrict __h_errnop);
extern int __gethostbyname2_r(const char *name, int af,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop);
extern int __gethostbyaddr_r(const void *addr, socklen_t len, int type,
        struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop);
extern int __epoll_wait_nocancel(int epfd, struct epoll_event *events,
        int maxevents, int timeout);
#elif defined(LIBGO_SYS_FreeBSD)
#endif
}

namespace co {

void __initStaticHook() {
    long ignores = 0;
    ignores += reinterpret_cast<long>(&__pipe);
    ignores += reinterpret_cast<long>(&__pipe2);
    ignores += reinterpret_cast<long>(&__socket);
    ignores += reinterpret_cast<long>(&__socketpair);
    ignores += reinterpret_cast<long>(&__connect);
    ignores += reinterpret_cast<long>(&__read);
    ignores += reinterpret_cast<long>(&__readv);
    ignores += reinterpret_cast<long>(&__recv);
    ignores += reinterpret_cast<long>(&__recvfrom);
    ignores += reinterpret_cast<long>(&__recvmsg);
    ignores += reinterpret_cast<long>(&__write);
    ignores += reinterpret_cast<long>(&__writev);
    ignores += reinterpret_cast<long>(&__send);
    ignores += reinterpret_cast<long>(&__sendto);
    ignores += reinterpret_cast<long>(&__sendmsg);
    ignores += reinterpret_cast<long>(&__libc_accept);
    ignores += reinterpret_cast<long>(&__libc_poll);
    ignores += reinterpret_cast<long>(&__select);
    ignores += reinterpret_cast<long>(&__sleep);
    ignores += reinterpret_cast<long>(&__usleep);
    ignores += reinterpret_cast<long>(&__nanosleep);
    ignores += reinterpret_cast<long>(&__libc_close);
    ignores += reinterpret_cast<long>(&__fcntl);
    ignores += reinterpret_cast<long>(&__ioctl);
    ignores += reinterpret_cast<long>(&__getsockopt);
    ignores += reinterpret_cast<long>(&__setsockopt);
    ignores += reinterpret_cast<long>(&__dup);
    ignores += reinterpret_cast<long>(&__dup2);
    ignores += reinterpret_cast<long>(&__dup3);
    ignores += reinterpret_cast<long>(&__new_fclose);
#if defined(LIBGO_SYS_Linux)
    ignores += reinterpret_cast<long>(&__gethostbyname_r);
    ignores += reinterpret_cast<long>(&__gethostbyname2_r);
    ignores += reinterpret_cast<long>(&__gethostbyaddr_r);
    ignores += reinterpret_cast<long>(&__epoll_wait_nocancel);
#elif defined(LIBGO_SYS_FreeBSD)
#endif
    errno = (int)ignores;
}
} // namespace co
