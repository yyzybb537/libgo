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

    inline void lock()
    {
        int c = 0;
        while (std::atomic_flag_test_and_set_explicit(&lck, std::memory_order_acquire)) ++c;
        if (c > 10) {
//            assert(false);
        }
        DebugPrint(dbg_spinlock, "lock");
    }

    inline bool try_lock()
    {
        bool ret = !std::atomic_flag_test_and_set_explicit(&lck, std::memory_order_acquire);
        DebugPrint(dbg_spinlock, "trylock returns %s", ret ? "true" : "false");
        return ret;
    }
    
    inline void unlock()
    {
        std::atomic_flag_clear_explicit(&lck, std::memory_order_release);
        DebugPrint(dbg_spinlock, "unlock");
    }
};


} //namespace co
