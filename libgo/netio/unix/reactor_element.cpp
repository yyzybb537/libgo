#include "reactor_element.h"
#include <poll.h>
#include <algorithm>
#include "fd_context.h"
#include "reactor.h"

namespace co {

ReactorElement::ReactorElement(int fd) : fd_(fd)
{
}

void ReactorElement::OnClose()
{
    Trigger(nullptr, POLLNVAL);
}

bool ReactorElement::Add(Reactor * reactor, short int pollEvent, Entry const& entry)
{
    std::unique_lock<std::mutex> lock(mtx_);
    EntryList & entryList = SelectList(pollEvent);
    CheckExpire(entryList);
    entryList.push_back(entry);

    short int addEvent = pollEvent & (POLLIN | POLLOUT);
    if (addEvent == 0)
        addEvent |= POLLERR;

    short int promiseEvent = event_ | addEvent;
    addEvent = promiseEvent & ~event_; // 计算event真实的差异

    if (promiseEvent != event_) {
        if (!reactor->AddEvent(fd_, addEvent, promiseEvent)) {
            // add error.
            Rollback(entryList, entry);
            return false;
        }
        event_ = promiseEvent;
        return true;
    }

    DebugPrint(dbg_ioblock, "Reactor::Add fd = %d, pollEvent = %s, event_ = %s, needn't epoll_ctl",
            fd_, PollEvent2Str(pollEvent), PollEvent2Str(event_));
    return true;
}

void ReactorElement::Rollback(EntryList & entryList, Entry const& entry)
{
    auto itr = std::find(entryList.begin(), entryList.end(), entry);
    if (entryList.end() != itr)
        entryList.erase(itr);
}

void ReactorElement::Trigger(Reactor * reactor, short int pollEvent)
{
    std::unique_lock<std::mutex> lock(mtx_);

    short int errEvent = POLLERR | POLLHUP | POLLNVAL;
    short int promiseEvent = 0;

    DebugPrint(dbg_ioblock, "Trigger fd = %d, pollEvent = %s", fd_, PollEvent2Str(pollEvent));

    short int check = POLLIN | errEvent;
    if (pollEvent & check) {
        if (!in_.empty())
            DebugPrint(dbg_ioblock, "Trigger fd = %d, POLLIN.size = %d", fd_, (int)in_.size());

        TriggerListWithoutLock(pollEvent & check, in_);
    } else if (!in_.empty()) {
        promiseEvent |= POLLIN;
    }

    check = POLLOUT | errEvent;
    if (pollEvent & check) {
        if (!out_.empty())
            DebugPrint(dbg_ioblock, "Trigger fd = %d, POLLOUT.size = %d", fd_, (int)out_.size());

        TriggerListWithoutLock(pollEvent & check, out_);
    } else if (!out_.empty()) {
        promiseEvent |= POLLOUT;
    }

    check = POLLIN | POLLOUT | errEvent;
    if (pollEvent & check) {
        if (!inAndOut_.empty())
            DebugPrint(dbg_ioblock, "Trigger fd = %d, (POLLIN|POLLOUT).size = %d", fd_, (int)inAndOut_.size());

        TriggerListWithoutLock(pollEvent & check, inAndOut_);
    } else if (!inAndOut_.empty()) {
        promiseEvent |= POLLIN|POLLOUT;
    }

    check = errEvent;
    if (pollEvent & check) {
        if (!err_.empty())
            DebugPrint(dbg_ioblock, "Trigger fd = %d, POLLERR.size = %d", fd_, (int)err_.size());

        TriggerListWithoutLock(pollEvent & check, err_);
    } else if (!err_.empty()) {
        promiseEvent |= POLLERR;
    }

    short int delEvent = event_ & ~promiseEvent;
    if (promiseEvent != event_) {
        if (reactor && reactor->DelEvent(fd_, delEvent, promiseEvent))
            event_ = promiseEvent;
        return ;
    }

    DebugPrint(dbg_ioblock, "Reactor::Del fd = %d, pollEvent = %s, event_ = %s, needn't epoll_ctl",
            fd_, PollEvent2Str(pollEvent), PollEvent2Str(event_));
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

void ReactorElement::CheckExpire(EntryList & entryList)
{
    entryList.erase(std::remove_if(entryList.begin(), entryList.end(), [](Entry & entry){
                    return entry.suspendEntry_.IsExpire();
                }), entryList.end());
}

} // namespace co
