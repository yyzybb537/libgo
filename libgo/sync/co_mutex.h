#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include <queue>
#include "co_condition_variable.h"

namespace co
{

/// 协程锁
class CoMutex
{
    LFLock lock_;
    bool isLocked_;
    ConditionVariableAny cv_;

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
