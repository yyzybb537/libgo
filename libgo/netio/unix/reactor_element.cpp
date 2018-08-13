#include "reactor_element.h"
#include <poll.h>
#include <algorithm>

namespace co {

void ReactorElement::OnClose()
{
    Trigger(POLLERR);
}

short int ReactorElement::Add(short int pollEvent, Entry const& entry)
{
    std::unique_lock<std::mutex> lock(mtx_);
    EntryList & entryList = SelectList(pollEvent);
    entryList.push_back(entry);
    short int event = 0;
    if (!inAndOut_.empty())
        event |= POLLIN | POLLOUT;
    else {
        if (!in_.empty())
            event |= POLLIN;
        if (!out_.empty())
            event |= POLLOUT;
    }
    // TODO: 测试poll不监听任何事件时, 是否可以默认监听到POLLERR.
    return event;
}

void ReactorElement::Rollback(short int pollEvent, Entry const& entry)
{
    std::unique_lock<std::mutex> lock(mtx_);
    EntryList & entryList = SelectList(pollEvent);
    auto itr = std::find(entryList.begin(), entryList.end(), entry);
    if (entryList.end() != itr)
        entryList.erase(itr);
}

void ReactorElement::Trigger(short int pollEvent)
{
    std::unique_lock<std::mutex> lock(mtx_);

    if ((pollEvent & POLLIN) || (pollEvent & POLLERR))
        TriggerListWithoutLock(pollEvent & (POLLIN | POLLERR), in_);

    if ((pollEvent & POLLOUT) || (pollEvent & POLLERR))
        TriggerListWithoutLock(pollEvent & (POLLOUT | POLLERR), out_);

    if ((pollEvent & POLLIN) || (pollEvent & POLLOUT) || (pollEvent & POLLERR))
        TriggerListWithoutLock(pollEvent & (POLLIN | POLLOUT | POLLERR), inAndOut_);

    if (pollEvent & POLLERR)
        TriggerListWithoutLock(POLLERR, err_);
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
