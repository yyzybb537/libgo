#include "kqueue_reactor.h"
#if defined(LIBGO_SYS_FreeBSD)
#include "reactor_element.h"
#include "fd_context.h"
#include "hook_helper.h"
#include <poll.h>
#include <thread>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unordered_map>

namespace co {

const char* EpollCtlOp2Str(int op)
{
    switch (op) {
    LIBGO_E2S_DEFINE(EV_ADD);
    LIBGO_E2S_DEFINE(EV_DELETE);
    default:
        return "None";
    }
}

KqueueReactor::KqueueReactor()
{
    kq_ = kqueue();
    InitLoopThread();
}

bool KqueueReactor::AddEvent(int fd, short int addEvent, short int promiseEvent)
{
    struct kevent kev[3];
    int n = 0;
    if (addEvent & POLLIN) {
        EV_SET(&kev[n++], fd, EVFILT_READ, EV_ADD, 0, 0, reinterpret_cast<void*>((long)fd));
    }
    if (addEvent & POLLOUT) {
        EV_SET(&kev[n++], fd, EVFILT_WRITE, EV_ADD, 0, 0, reinterpret_cast<void*>((long)fd));
    }
    if (addEvent & POLLERR) {
        EV_SET(&kev[n++], fd, EVFILT_EXCEPT, EV_ADD, 0, 0, reinterpret_cast<void*>((long)fd));
    }

    int res = kevent(kq_, kev, n, nullptr, 0, nullptr);
    DebugPrint(dbg_ioblock, "KqueueReactor::ADD fd = %d, addEvent = %s, promiseEvent = %s, "
            "ret = %d, errno = %d",
            fd, PollEvent2Str(addEvent), PollEvent2Str(promiseEvent), res, errno);
    return res == 0;
}

bool KqueueReactor::DelEvent(int fd, short int delEvent, short int promiseEvent)
{
    struct kevent kev[3];
    int n = 0;
    if (delEvent & POLLIN) {
        EV_SET(&kev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, reinterpret_cast<void*>((long)fd));
    }
    if (delEvent & POLLOUT) {
        EV_SET(&kev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, reinterpret_cast<void*>((long)fd));
    }
    if (delEvent & POLLERR) {
        EV_SET(&kev[n++], fd, EVFILT_EXCEPT, EV_DELETE, 0, 0, reinterpret_cast<void*>((long)fd));
    }

    int res = kevent(kq_, kev, n, nullptr, 0, nullptr);
    DebugPrint(dbg_ioblock, "KqueueReactor::DEL fd = %d, delEvent = %s, promiseEvent = %s, "
            "ret = %d, errno = %d",
            fd, PollEvent2Str(delEvent), PollEvent2Str(promiseEvent),
            res, errno);
    return res == 0;
}

void KqueueReactor::Run()
{
    const int cEvent = 1024;
    struct kevent kev[cEvent];
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 10 * 1000 * 1000;
    int n = kevent(kq_, nullptr, 0, kev, cEvent, &timeout);
    std::unordered_map<int, short int> eventMap;
    for (int i = 0; i < n; ++i) {
        struct kevent & ev = kev[i];

        int fd = (int)reinterpret_cast<long>(ev.udata);

        short int pollEvent = 0;
        if (ev.filter == EVFILT_READ)
            pollEvent = POLLIN;
        else if (ev.filter == EVFILT_WRITE)
            pollEvent = POLLOUT;

        if (ev.flags & EV_EOF)
            pollEvent |= POLLHUP;

        if (ev.flags & EV_ERROR)
            pollEvent |= POLLERR;

        eventMap[fd] |= pollEvent;
    }

    for (auto & kv : eventMap) {
        FdContextPtr ctx = HookHelper::getInstance().GetFdContext(kv.first);
        if (!ctx)
            continue;

        ctx->Trigger(this, kv.second);
    }
}

} // namespace co
#endif
