#pragma once
#include <stdint.h>
#include <libgo/block_object.h>


namespace co
{

/// 协程锁
class CoMutex
{
    std::shared_ptr<BlockObject> block_;

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
