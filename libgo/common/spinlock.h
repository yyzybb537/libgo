#pragma once
#include "config.h"
#include <exception>

namespace co
{

struct BooleanFakeLock
{
    bool locked_ = false;

    ALWAYS_INLINE void lock()
    {
        while (!locked_) locked_ = true;
        DebugPrint(dbg_spinlock, "lock");
    }

    ALWAYS_INLINE bool is_lock()
    {
        return locked_;
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

// 性能高于LFLock2
struct LFLock
{
    std::atomic_flag flag;

    LFLock() : flag{false}
    {
    }

    ALWAYS_INLINE void lock()
    {
        while (flag.test_and_set(std::memory_order_acquire)) ;
    }

    // PS: 这是一个可能没有内存一致性的接口, 使用的时候要小心cpu缓存不能及时刷新的问题.
    ALWAYS_INLINE bool is_lock()
    {
#if defined(LIBGO_SYS_Linux)
        return flag._M_i;
#elif defined(LIBGO_SYS_FreeBSD)
        return flag.__a_;
#endif
    }

    ALWAYS_INLINE bool try_lock()
    {
        return !flag.test_and_set(std::memory_order_acquire);
    }
    
    ALWAYS_INLINE void unlock()
    {
        flag.clear(std::memory_order_release);
    }
};

struct FakeLock {
    void lock() {}
    bool is_lock() { return false; }
    bool try_lock() { return true; }
    void unlock() {}
};

// atomic_bool可能不是无锁
struct LFLock2
{
    std::atomic_bool state;

    LFLock2() : state{false}
    {
    }

    ALWAYS_INLINE void lock()
    {
        while (state.exchange(true, std::memory_order_acquire)) ;
    }

    ALWAYS_INLINE bool try_lock()
    {
        return !state.exchange(true, std::memory_order_acquire);
    }
    
    ALWAYS_INLINE void unlock()
    {
        if (!state.exchange(false, std::memory_order_release)) {
            throw std::logic_error("libgo.spinlock unlock exception: state == false");
        }
    }

    ALWAYS_INLINE bool is_lock()
    {
        return state;
    }
};

} //namespace co
