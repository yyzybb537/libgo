#pragma once
#include "../../common/config.h"
#include "../../scheduler/processer.h"

namespace co {

class Reactor;
class ReactorElement
{
public:
    struct Entry {
        Processer::SuspendEntry suspendEntry_;
        std::shared_ptr<short int> revents_;
        int idx_;

        Entry() {}
        Entry(Processer::SuspendEntry const& suspendEntry,
                std::shared_ptr<short int> const& revents,
                int idx)
            : suspendEntry_(suspendEntry), revents_(revents), idx_(idx)
        {}

        friend bool operator==(Entry const& lhs, Entry const& rhs) {
            return lhs.idx_ == rhs.idx_ &&
                lhs.revents_ == rhs.revents_ &&
                lhs.suspendEntry_ == rhs.suspendEntry_;
        }
    };
    typedef std::vector<Entry> EntryList;

    explicit ReactorElement(int fd);

    bool Add(Reactor * reactor, short int pollEvent, Entry const& entry);

    void Trigger(Reactor * reactor, short int pollEvent);

protected:
    void OnClose();

    EntryList & SelectList(short int pollEvent);

    void TriggerListWithoutLock(short int revent, EntryList & entryList);

    void Rollback(EntryList & entryList, Entry const& entry);

    void CheckExpire(EntryList & entryList);

private:
    std::mutex mtx_;

    int fd_;
    short int event_ = 0;

    EntryList in_;
    EntryList out_;
    EntryList inAndOut_;
    EntryList err_;
};

} // namespace co
