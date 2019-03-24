#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include "../common/ts_queue.h"
#include <condition_variable>

namespace co
{

class condition_variable_v2
{
    struct Entry : public TSQueueHook
    {
        Processer::SuspendEntry entry;
        
        LFLock notified;
    };

    TSQueue<Entry> queue_;

public:
    template <typename LockType>
    std::cv_status wait(LockType & lock)
    {
        Entry* entry = new Entry;
        entry->entry = Processer::Suspend();
        queue_.push(entry);
        lock.unlock();
        Processer::StaticCoYield();
//        lock.lock();
        return std::cv_status::no_timeout;
    }

    bool notify_one()
    {
        for (;;) {
            Entry* entry = queue_.pop();
            if (!entry) return false;

            std::unique_ptr<Entry> ep(entry);
            if (!entry->notified.try_lock())
                continue;

            if (Processer::Wakeup(entry->entry))
                return true;
        }

        return false;
    }

    size_t notify_all()
    {
        size_t n = 0;
        while (notify_one())
            ++n;
        return n;
    }
};

} // namespace co
