#pragma once
#include "rutex.h"

namespace libgo
{

// Implement bthread_mutex_t related functions
struct MutexInternal {
    std::atomic<unsigned char> locked;
    std::atomic<unsigned char> contended;
    unsigned short padding;
};

const MutexInternal k_mutex_contended_raw = {{1},{1},0};
const MutexInternal k_mutex_locked_raw = {{1},{0},0};

// Define as macros rather than constants which can't be put in read-only
// section and affected by initialization-order fiasco.
#define LIBGO_ROUTINE_SYNC_MUTEX_CONTENDED (*(const unsigned*)&::libgo::k_mutex_contended_raw)
#define LIBGO_ROUTINE_SYNC_MUTEX_LOCKED (*(const unsigned*)&::libgo::k_mutex_locked_raw)

static_assert(sizeof(unsigned) == sizeof(MutexInternal),
              "sizeof(MutexInternal) must equal sizeof(unsigned)");

struct Mutex : public DebuggerId<Mutex>
{
public:
    typedef Rutex<unsigned> RutexType;

    Mutex() noexcept {}
    ~Mutex() noexcept {}

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock()
    {
        lock(RoutineSyncTimer::null_tp());
    }

    bool try_lock()
    {
        MutexInternal* split = (MutexInternal*)rutex_.value();
        return 0 == split->locked.exchange(1, std::memory_order_acquire);
    }

    bool is_lock()
    {
        MutexInternal* split = (MutexInternal*)rutex_.value();
        return split->locked == 1;
    }

    void unlock()
    {
        RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s",
                    id(), rutex_.id(), __func__);

        std::atomic<unsigned>* whole = (std::atomic<unsigned>*)rutex_.value();
        const unsigned prev = whole->exchange(0, std::memory_order_release);
        if (prev == LIBGO_ROUTINE_SYNC_MUTEX_LOCKED) {
            RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s | not contended",
                    id(), rutex_.id(), __func__);
            return ;
        }

        int res = rutex_.notify_one();
        (void)res;
        RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s | contended | notify_one return %d",
                    id(), rutex_.id(), __func__, res);
    }

    template <typename _Clock, typename _Duration>
    bool try_lock_until(const std::chrono::time_point<_Clock, _Duration> & abstime)
    {
        return lock(&abstime);
    }

    template <class Rep, class Period>
    bool try_lock_for( const std::chrono::duration<Rep,Period>& timeout_duration )
    {
        auto abstime = RoutineSyncTimer::now() + timeout_duration;
        return try_lock_until(&abstime);
    }

public:
    template <typename _Clock, typename _Duration>
    bool lock(const std::chrono::time_point<_Clock, _Duration> * abstime)
    {
        if (try_lock()) {
            RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s | abstime=%d | try_lock success",
                    id(), rutex_.id(), __func__, !!abstime);
            return true;
        }
        
        int res = lock_contended(abstime);
        RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s | abstime=%d | lock_contended %s",
                    id(), rutex_.id(), __func__, !!abstime,
                    res == RutexBase::rutex_wait_return_success ? "success" : "timeout");
        return RutexBase::rutex_wait_return_success == res;
    }

    template<typename _Clock, typename _Duration>
    inline int lock_contended(const std::chrono::time_point<_Clock, _Duration> * abstime) {
        std::atomic<unsigned>* whole = rutex_.value();
        while (whole->exchange(LIBGO_ROUTINE_SYNC_MUTEX_CONTENDED) & LIBGO_ROUTINE_SYNC_MUTEX_LOCKED) {
            RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s | abstime=%d | begin wait_until",
                    id(), rutex_.id(), __func__, !!abstime);

            int res = rutex_.wait_until(LIBGO_ROUTINE_SYNC_MUTEX_CONTENDED, abstime);
            
            RS_DBG(dbg_mutex, "mutex=%ld rutex=%ld | %s | abstime=%d | wait_until return %d",
                id(), rutex_.id(), __func__, !!abstime, res);

            if (RutexBase::rutex_wait_return_etimeout == res) {
                return res;
            }
        }
        return RutexBase::rutex_wait_return_success;
    }

    template <typename RutexT>
    int requeue_in_lock(RutexT *src)
    {
        int n = src->requeue(native());
        if (n > 0) {
            MutexInternal* split = (MutexInternal*)rutex_.value();
            split->contended.exchange(1, std::memory_order_relaxed);
        }
        return n;
    }

    RutexType* native() { return &rutex_; }

private:
    RutexType rutex_;
};

} // namespace libgo
