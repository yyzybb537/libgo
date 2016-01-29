#pragma once
#include <atomic>
#include <assert.h>

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
    }

    inline bool try_lock()
    {
        return !std::atomic_flag_test_and_set_explicit(&lck, std::memory_order_acquire);
    }
    
    inline void unlock()
    {
        std::atomic_flag_clear_explicit(&lck, std::memory_order_release);
    }
};


} //namespace co
