#include "reactor_element.h"
#include <poll.h>
#include <algorithm>
#include <sys/epoll.h>
#include "fd_context.h"

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

ReactorElement::ReactorElement(int fd) : fd_(fd)
{
}

void ReactorElement::OnClose()
{
    Trigger(-1, POLLNVAL);
}

bool ReactorElement::Add(int epfd, short int pollEvent, Entry const& entry)
{
    std::unique_lock<std::mutex> lock(mtx_);
    EntryList & entryList = SelectList(pollEvent);
    entryList.push_back(entry);
    short int event = event_ | (pollEvent & (POLLIN | POLLOUT));

    // TODO: 测试poll不监听任何事件时, 是否可以默认监听到POLLERR.

    if (event != event_) {
        struct epoll_event ev;
        ev.events = PollEvent2EpollEvent(event);
        ev.data.fd = fd_;
        int op = event_ == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
        int res = CallWithoutINTR<int>(::epoll_ctl, epfd, op, fd_, &ev);
        DebugPrint(dbg_ioblock, "Reactor::Add fd = %d, pollEvent = %s, modEvent=(%s) -> (%s), "
                "epoll_ctl op = %s, ret = %d, errno = %d",
                fd_, PollEvent2Str(pollEvent), PollEvent2Str(event_), PollEvent2Str(event),
                EpollCtlOp2Str(op), res, errno);
        if (res == -1 && errno != EEXIST) {
            // add error.
            Rollback(entryList, entry);
            return false;
        }
        event_ = event;
        return true;
    }

    DebugPrint(dbg_ioblock, "Reactor::Add fd = %d, pollEvent = %s, modEvent=(%s) -> (%s), needn't epoll_ctl",
            fd_, PollEvent2Str(pollEvent), PollEvent2Str(event_), PollEvent2Str(event));
    return true;
}

void ReactorElement::Rollback(EntryList & entryList, Entry const& entry)
{
    auto itr = std::find(entryList.begin(), entryList.end(), entry);
    if (entryList.end() != itr)
        entryList.erase(itr);
}

void ReactorElement::Trigger(int epfd, short int pollEvent)
{
    std::unique_lock<std::mutex> lock(mtx_);

    int triggerIn = 0, triggerOut = 0, triggerInOut = 0, triggerErr = 0;

    short int check = POLLIN | POLLERR | POLLHUP | POLLNVAL;
    if (pollEvent & check) {
        triggerIn = in_.size();
        TriggerListWithoutLock(pollEvent & check, in_);
    }

    check = POLLOUT | POLLERR | POLLHUP | POLLNVAL;
    if (pollEvent & check) {
        triggerOut = out_.size();
        TriggerListWithoutLock(pollEvent & check, out_);
    }

    check = POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL;
    if (pollEvent & check) {
        triggerInOut = inAndOut_.size();
        TriggerListWithoutLock(pollEvent & check, inAndOut_);
    }

    check = POLLERR | POLLHUP | POLLNVAL;
    if (pollEvent & check) {
        triggerErr = err_.size();
        TriggerListWithoutLock(pollEvent & check, err_);
    }

    short int event = event_ & ~(pollEvent & (POLLIN | POLLOUT));
    int res = 0;
    int op = 0;
    if (epfd != -1 && event != event_) {
        op = event == 0 ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
        struct epoll_event ev;
        ev.events = PollEvent2EpollEvent(event);
        ev.data.fd = fd_;
        res = CallWithoutINTR<int>(::epoll_ctl, epfd, op, fd_, &ev);
    }

    DebugPrint(dbg_ioblock, "%sReactorElement::Trigger epfd = %d, fd = %d, pollEvent = %s, modEvent=(%s) -> (%s), "
            "trigger (in,out,inout,err) = (%d,%d,%d,%d), epoll_ctl op = %s, ret = %d, errno = %d",
            epfd == -1 ? "Destruct " : "",
            epfd, fd_, PollEvent2Str(pollEvent), PollEvent2Str(event_), PollEvent2Str(event),
            triggerIn, triggerOut, triggerInOut, triggerErr,
            EpollCtlOp2Str(op), res, errno);
    event_ = event;
}

void ReactorElement::TriggerListWithoutLock(short int revent, EntryList & entryList)
{
    for (Entry & entry : entryList) {
        entry.revents_.get()[entry.idx_] = revent;
        Processer::Wakeup(entry.suspendEntry_);
    }
    entryList.clear();
}

ReactorElement::EntryList & ReactorElement::SelectList(short int pollEvent)
{
    EntryList * pEntry = nullptr;
    if ((pollEvent & POLLIN) && (pollEvent & POLLOUT)) {
        pEntry = &inAndOut_;
    } else if (pollEvent & POLLIN) {
        pEntry = &in_;
    } else if (pollEvent & POLLOUT) {
        pEntry = &out_;
    } else {
        pEntry = &err_;
    }
    return *pEntry;
}

} // namespace co
