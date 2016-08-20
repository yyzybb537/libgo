#pragma once
#include <libgo/config.h>

namespace co
{

#if LIBGO_SINGLE_THREAD
struct LFLock
{
    bool locked_ = false;

    ALWAYS_INLINE void lock()
    {
        while (!locked_) locked_ = true;
        DebugPrint(dbg_spinlock, "lock");
    }

    ALWAYS_INLINE bool try_lock()
    {
        bool ret = !locked_;
        if (ret) locked_ = true;
        DebugPrint(dbg_spinlock, "trylock returns %s", ret ? "true" : "false");
        return ret;
    }
    
    ALWAYS_INLINE void unlock()
    {
        assert(locked_);
        locked_ = false;
        DebugPrint(dbg_spinlock, "unlock");
    }
};
#else //LIBGO_SINGLE_THREAD
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
#endif //LIBGO_SINGLE_THREAD

} //namespace co
