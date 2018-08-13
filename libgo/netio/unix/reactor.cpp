#include "reactor.h"
#include "fd_context.h"
#include "hook_helper.h"
#include <sys/epoll.h>
#include <poll.h>
#include <thread>

namespace co {

std::vector<Reactor*> Reactor::sReactors_;

Reactor& Reactor::Select(int fd)
{
    static int ignore = InitializeReactorCount(1);
    (void)ignore;
    return *sReactors_[fd % sReactors_.size()];
}

int Reactor::InitializeReactorCount(uint8_t n)
{
    if (!sReactors_.empty()) return 0;
    sReactors_.reserve(n);
    for (uint8_t i = 0; i < n; i++) {
        sReactors_.push_back(new Reactor);
    }
    return 0;
}

Reactor::Reactor()
{
    epfd_ = epoll_create(1024);

    std::thread thr([this]{
                for (;;) this->Run();
            });
    thr.detach();
}

bool Reactor::Add(int fd, short int pollEvent, Entry const& entry)
{
    FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
    if (!ctx) return false;

    short int event = ctx->Add(pollEvent, entry);
    struct epoll_event ev;
    ev.events = PollEvent2EpollEvent(event) | EPOLLONESHOT;
    ev.data.fd = fd;
    int res = CallWithoutINTR<int>(::epoll_ctl, epfd_, EPOLL_CTL_ADD, fd, &ev);
    if (res == -1 && errno != EEXIST) {
        // add error.
        ctx->Rollback(pollEvent, entry);
        return false;
    }
    return true;
}

void Reactor::Run()
{
    const int cEvent = 1024;
    struct epoll_event evs[cEvent];
    int n = CallWithoutINTR<int>(::epoll_wait, epfd_, evs, cEvent, 10);
    for (int i = 0; i < n; ++n) {
        struct epoll_event & ev = evs[i];
        int fd = ev.data.fd;
        FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
        if (!ctx)
            continue;

        ctx->Trigger(EpollEvent2PollEvent(ev.events));
    }
}

uint32_t Reactor::PollEvent2EpollEvent(short int pollEvent)
{
    uint32_t epollEvent = 0;
    if (pollEvent & POLLIN)
        epollEvent |= EPOLLIN;
    if (pollEvent & POLLOUT)
        epollEvent |= EPOLLOUT;
    if (pollEvent & POLLERR)
        epollEvent |= EPOLLERR;
    return epollEvent;
}

short int Reactor::EpollEvent2PollEvent(uint32_t epollEvent)
{
    short int pollEvent = 0;
    if (epollEvent & EPOLLIN)
        pollEvent |= POLLIN;
    if (epollEvent & EPOLLOUT)
        pollEvent |= POLLOUT;
    if (epollEvent & EPOLLERR)
        pollEvent |= POLLERR;
    return pollEvent;
}

} // namespace co
