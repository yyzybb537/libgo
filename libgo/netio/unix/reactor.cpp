#include "reactor.h"
#include "fd_context.h"
#include "hook_helper.h"
#include <poll.h>
#include <thread>
#include "epoll_reactor.h"
#include "kqueue_reactor.h"

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
#if defined(LIBGO_SYS_Linux)
        sReactors_.push_back(new EpollReactor);
#elif defined(LIBGO_SYS_FreeBSD)
        sReactors_.push_back(new KqueueReactor);
#endif
    }
    return 0;
}

int Reactor::GetReactorThreadCount()
{
    return sReactors_.size();
}

Reactor::Reactor()
{
}

void Reactor::InitLoopThread()
{
    std::thread thr([this]{
                DebugPrint(dbg_thread, "Start reactor(epoll/kqueue) thread id: %lu", NativeThreadID());
                for (;;) this->Run();
            });
    thr.detach();
}

bool Reactor::Add(int fd, short int pollEvent, Entry const& entry)
{
    FdContextPtr ctx = HookHelper::getInstance().GetFdContext(fd);
    if (!ctx) return false;

    return ctx->Add(this, pollEvent, entry);
}

} // namespace co
