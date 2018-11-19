#include "../common/error.h"
#include "co_condition_variable.h"

namespace co
{

ConditionVariableAny::ConditionVariableAny()
{
    checkIter_ = queue_.begin();
}

ConditionVariableAny::~ConditionVariableAny()
{
    std::unique_lock<LFLock> lock(lock_);

    while (!queue_.empty()) {
        if (checkIter_ == queue_.begin())
            ++checkIter_;

        Entry entry = queue_.front();
        queue_.pop_front();

        if (!entry.noTimeoutLock->try_lock()) {
            continue;
        }

        if (entry.suspendEntry && entry.suspendEntry.IsExpire()) {
            continue;
        }

        assert(false);
        ThrowException("libgo.ConditionVariableAny still have waiters when deconstructed.");
    }
}

bool ConditionVariableAny::notify_one()
{
    std::unique_lock<LFLock> lock(lock_);

    while (!queue_.empty()) {
        if (checkIter_ == queue_.begin())
            ++checkIter_;

        Entry entry = queue_.front();
        queue_.pop_front();

        if (!entry.noTimeoutLock->try_lock()) {
            continue;
        }

        if (!entry.suspendEntry) {
            // 原生线程
            cv_.notify_one();
            if (entry.onWakeup)
                entry.onWakeup();
            return true;
        }

        if (!Processer::Wakeup(entry.suspendEntry)) {
            continue;
        }

        if (entry.onWakeup)
            entry.onWakeup();
        return true;
    }

    return false;
}

size_t ConditionVariableAny::notify_all()
{
    std::unique_lock<LFLock> lock(lock_);

    size_t n = 0;
    while (!queue_.empty()) {
        if (checkIter_ == queue_.begin())
            ++checkIter_;

        Entry entry = queue_.front();
        queue_.pop_front();

        if (!entry.noTimeoutLock->try_lock()) {
            continue;
        }

        if (!entry.suspendEntry) {
            // 原生线程
            cv_.notify_one();
            ++n;
            if (entry.onWakeup)
                entry.onWakeup();
            continue;
        }

        if (!Processer::Wakeup(entry.suspendEntry)) {
            continue;
        }

        if (entry.onWakeup)
            entry.onWakeup();
        ++n;
    }

    return n;
}

bool ConditionVariableAny::empty()
{
    std::unique_lock<LFLock> lock(lock_);
    return queue_.empty();
}

void ConditionVariableAny::AddWaiter(Entry const& entry) {
    std::unique_lock<LFLock> lock(lock_);
    queue_.push_back(entry);

    if (queue_.size() < 8) {
        return ;
    }

    // 每次新增时, check大于1个entry的有效性, 即可防止泄露
    if (checkIter_ == queue_.end())
        checkIter_ = queue_.begin();

    for (int i = 0; i < 2 && checkIter_ != queue_.end(); ++i) {
        Entry & entry = *checkIter_;
        if (entry.suspendEntry && entry.suspendEntry.IsExpire()) {
            queue_.erase(checkIter_++);
            continue;
        }

        ++checkIter_;
    }
}

} //namespace co
