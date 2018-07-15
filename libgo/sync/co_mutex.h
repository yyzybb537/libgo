#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include <queue>
#include <condition_variable>

namespace co
{

/// 协程锁
class CoMutex
{
    LFLock lock_;
    std::queue<Processer::SuspendEntry> queue_;
    bool isLocked_;

    // 兼容原生线程
    std::condition_variable_any cv_;

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
