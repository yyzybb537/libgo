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

        std::shared_ptr<std::timed_mutex> thread_entry;
    };

    TSQueue<Entry> queue_;

public:
    template <typename LockType>
    std::cv_status wait(LockType & lock)
    {
        Entry* entry = new Entry;

        if (Processer::IsCoroutine()) {
            entry->entry = Processer::Suspend();
            queue_.push(entry);
            lock.unlock();
            Processer::StaticCoYield();
        } else {
            entry->thread_entry.reset(new std::timed_mutex);
            entry->thread_entry->lock();
            auto locker = entry->thread_entry;
            queue_.push(entry);
            lock.unlock();
            locker->lock();
        }

        lock.lock();
        return std::cv_status::no_timeout;
    }

    template <typename LockType, typename TimePointType>
    std::cv_status wait_util(LockType & lock, TimePointType timepoint)
    {
        Entry* entry = new Entry;

        if (Processer::IsCoroutine()) {
            entry->entry = Processer::Suspend(timepoint);
            queue_.push(entry);
            lock.unlock();
            Processer::StaticCoYield();
        } else {
            entry->thread_entry.reset(new std::timed_mutex);
            entry->thread_entry->lock();
            auto locker = entry->thread_entry;
            queue_.push(entry);
            lock.unlock();
            locker->try_lock_until(timepoint);
        }

        lock.lock();
        return entry->notified.try_lock() ?
            std::cv_status::timeout : std::cv_status::no_timeout;
    }

    bool notify_one()
    {
        for (;;) {
            Entry* entry = queue_.pop();
            if (!entry) return false;

            std::unique_ptr<Entry> ep(entry);
            if (!entry->notified.try_lock())
                continue;

            if (entry->thread_entry) {
                entry->thread_entry->unlock();
                return true;
            } else if (Processer::Wakeup(entry->entry))
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
