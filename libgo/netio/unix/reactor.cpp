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
                DebugPrint(dbg_thread, "Start epoll thread id: %lu", NativeThreadID());
                for (;;) this->Run();
            });
    thr.detach();
}

bool Reactor::Add(int fd, short int pollEvent, Entry const& entry)
{
    FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
    if (!ctx) return false;

    return ctx->Add(epfd_, pollEvent, entry);
}

void Reactor::Run()
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

        ctx->Trigger(epfd_, EpollEvent2PollEvent(ev.events));
    }
}

} // namespace co
