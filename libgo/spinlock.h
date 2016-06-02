#pragma once
#include <atomic>
#include <assert.h>
#include "config.h"

namespace co
{

struct LFLock
{
    volatile std::atomic_flag lck;

    LFLock() 
    {
        lck.clear();
    }

    ALWAYS_INLINE void lock()
    {
        while (std::atomic_flag_test_and_set_explicit(&lck, std::memory_order_acquire)) ;
        DebugPrint(dbg_spinlock, "lock");
    }

    ALWAYS_INLINE bool try_lock()
    {
        bool ret = !std::atomic_flag_test_and_set_explicit(&lck, std::memory_order_acquire);
        DebugPrint(dbg_spinlock, "trylock returns %s", ret ? "true" : "false");
        return ret;
    }
    
    ALWAYS_INLINE void unlock()
    {
        std::atomic_flag_clear_explicit(&lck, std::memory_order_release);
        DebugPrint(dbg_spinlock, "unlock");
    }
};


} //namespace co
