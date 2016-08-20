#if __linux__
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <libgo/linux_glibc_hook.h>
#endif

namespace co {

    void coroutine_hook_init()
    {
    }

} //namespace co

#if __linux__

extern "C" {

connect_t connect_f = &connect;
read_t read_f = &read;
readv_t readv_f = &readv;
recv_t recv_f = &recv;
recvfrom_t recvfrom_f = &recvfrom;
recvmsg_t recvmsg_f = &recvmsg;
write_t write_f = &write;
writev_t writev_f = &writev;
send_t send_f = &send;
sendto_t sendto_f = &sendto;
sendmsg_t sendmsg_f = &sendmsg;
poll_t poll_f = &poll;
select_t select_f = &select;
accept_t accept_f = &accept;
sleep_t sleep_f = &sleep;
usleep_t usleep_f = &usleep;
nanosleep_t nanosleep_f = &nanosleep;
close_t close_f = &close;
fcntl_t fcntl_f = &fcntl;
ioctl_t ioctl_f = &ioctl;
getsockopt_t getsockopt_f = &getsockopt;
setsockopt_t setsockopt_f = &setsockopt;
dup_t dup_f = &dup;
dup2_t dup2_f = &dup2;
dup3_t dup3_f = &dup3;

} //extern "C"

namespace co
{
    void set_connect_timeout(int milliseconds)
    {
    }

} //namespace co

#endif
