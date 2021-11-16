#pragma once
#include "../common/config.h"

#if USE_ROUTINE_SYNC
# include "../routine_sync/mutex.h"

namespace co
{

typedef libgo::Mutex CoMutex;
typedef CoMutex co_mutex;

} //namespace co

#else

# include "../scheduler/processer.h"
# include <queue>
# include "co_condition_variable.h"

namespace co
{

/// 协程锁
class CoMutex
{
    typedef std::mutex lock_t;
    lock_t lock_;
    bool notified_ = false;
    ConditionVariableAny cv_;

    std::atomic_long sem_;

public:
    CoMutex();
    ~CoMutex();

    void lock();
    bool try_lock();
    bool is_lock();
    void unlock();
};

typedef CoMutex co_mutex;

} //namespace co
#endif //USE_ROUTINE_SYNC
