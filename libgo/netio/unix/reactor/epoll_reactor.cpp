#include "epoll_reactor.h"
#if defined(LIBGO_SYS_Linux)
#include "reactor_element.h"
#include "fd_context.h"
#include "hook_helper.h"
#include <poll.h>
#include <thread>
#include <sys/epoll.h>

namespace co {

const char* EpollCtlOp2Str(int op)
{
    switch (op) {
    LIBGO_E2S_DEFINE(EPOLL_CTL_ADD);
    LIBGO_E2S_DEFINE(EPOLL_CTL_MOD);
    LIBGO_E2S_DEFINE(EPOLL_CTL_DEL);
    default:
        return "None";
    }
}

uint32_t PollEvent2ReactorEvent(short int pollEvent)
{
    uint32_t reactorEvent = 0;
    if (pollEvent & POLLIN)
        reactorEvent |= EPOLLIN;
    if (pollEvent & POLLOUT)
        reactorEvent |= EPOLLOUT;
    if (pollEvent & POLLERR)
        reactorEvent |= EPOLLERR;
    if (pollEvent & POLLHUP)
        reactorEvent |= EPOLLHUP;
    return reactorEvent;
}

short int ReactorEvent2PollEvent(uint32_t reactorEvent)
{
    short int pollEvent = 0;
    if (reactorEvent & EPOLLIN)
        pollEvent |= POLLIN;
    if (reactorEvent & EPOLLOUT)
        pollEvent |= POLLOUT;
    if (reactorEvent & EPOLLERR)
        pollEvent |= POLLERR;
    if (reactorEvent & EPOLLHUP)
        pollEvent |= POLLHUP;
    return pollEvent;
}

EpollReactor::EpollReactor()
{
    epfd_ = epoll_create(1024);
    InitLoopThread();
}

bool EpollReactor::AddEvent(int fd, short int addEvent, short int promiseEvent)
{
    struct epoll_event ev;
    ev.events = PollEvent2ReactorEvent(promiseEvent) | EPOLLET;
    ev.data.fd = fd;
    int op = addEvent == promiseEvent ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    int res = CallWithoutINTR<int>(::epoll_ctl, epfd_, op, fd, &ev);
    DebugPrint(dbg_ioblock, "EpollReactor::ADD fd = %d, addEvent = %s, promiseEvent = %s, "
            "epoll_ctl op = %s, ret = %d, errno = %d",
            fd, PollEvent2Str(addEvent), PollEvent2Str(promiseEvent),
            EpollCtlOp2Str(op), res, errno);
    return res == 0;
}

bool EpollReactor::DelEvent(int fd, short int delEvent, short int promiseEvent)
{
    struct epoll_event ev;
    ev.events = PollEvent2ReactorEvent(promiseEvent) | EPOLLET;
    ev.data.fd = fd;
    int op = promiseEvent == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    int res = CallWithoutINTR<int>(::epoll_ctl, epfd_, op, fd, &ev);
    DebugPrint(dbg_ioblock, "EpollReactor::DEL fd = %d, delEvent = %s, promiseEvent = %s, "
            "epoll_ctl op = %s, ret = %d, errno = %d",
            fd, PollEvent2Str(delEvent), PollEvent2Str(promiseEvent),
            EpollCtlOp2Str(op), res, errno);
    return res == 0;
}

void EpollReactor::Run()
{
    const int cEvent = 1024;
    struct epoll_event evs[cEvent];
    int n = CallWithoutINTR<int>(::epoll_wait, epfd_, evs, cEvent, 10);
    for (int i = 0; i < n; ++i) {
        struct epoll_event & ev = evs[i];
        int fd = ev.data.fd;
        FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
        if (!ctx)
            continue;

        ctx->Trigger(this, ReactorEvent2PollEvent(ev.events));
    }
}

} // namespace co
#endif
