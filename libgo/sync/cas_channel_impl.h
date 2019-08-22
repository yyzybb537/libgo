#pragma once
#include "../common/config.h"
#include "channel_impl.h"

namespace co
{

template <typename T>
class CASChannelImpl : public ChannelImpl<T>
{
    typedef std::mutex lock_t;
    typedef FastSteadyClock::time_point time_point_t;
    const std::size_t capacity_;
    bool closed_;
    uint64_t dbg_mask_;

    struct Entry {
        int id;
        T* pvalue;
        T value;

        Entry() : pvalue(nullptr) {}
    };

    typedef ConditionVariableAnyT<Entry> cond_t;
    cond_t wq_;
    cond_t rq_;

    // number of read wait << 32 | number of write wait
    atomic_t<uint64_t> wait_ {0};
    static const uint64_t write1 = 1;
    static const uint64_t writeMask = 0xffffffff;
    static const uint64_t read1 = ((size_t)1) << 32;
    static const uint64_t readMask = 0xffffffff00000000;
    static const int kSpinCount = 4000;

public:
    explicit CASChannelImpl(std::size_t capacity)
        : capacity_(capacity), closed_(false), dbg_mask_(dbg_all)
        , wq_(capacity ? capacity + 1 : (size_t)-1, NULL)
    {
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel init. capacity=%lu", this->getId(), capacity);
    }
    
    // write
    bool Push(T t, bool bWait, FastSteadyClock::time_point deadline = FastSteadyClock::time_point{})
    {
        DebugPrint(dbg_channel, "[id=%ld] Push ->", this->getId());

        if (closed_) {
            DebugPrint(dbg_channel, "[id=%ld] Push by closed", this->getId());
            return false;
        }

        int spin = 0;

        uint64_t wait;
        for (;;) {
            wait = wait_.load(std::memory_order_relaxed);
            if (wait & readMask) {
                if (!rq_.notify_one(
                            [&](Entry & entry)
                            {
                                *entry.pvalue = t;
                            }))
                {
                    if (++spin >= kSpinCount) {
                        spin = 0;
//                            printf("spin by push\n");
                        Processer::StaticCoYield();
                    }

                    if (capacity_ == 0) {
                        if (closed_) {
                            DebugPrint(dbg_channel, "[id=%ld] Push failed by closed.", this->getId());
                            return false;
                        } else if (!bWait) {
                            DebugPrint(dbg_channel, "[id=%ld] TryPush failed.", this->getId());
                            return false;
                        }
                    }
                    continue;
                }
                DebugPrint(dbg_channel, "[id=%ld] Push Notify.", this->getId());

                wait_ -= read1;
                return true;
            } else if (capacity_ == 0) {
                if (closed_) {
                    DebugPrint(dbg_channel, "[id=%ld] Push failed by closed.", this->getId());
                    return false;
                } else if (!bWait) {
                    DebugPrint(dbg_channel, "[id=%ld] TryPush failed.", this->getId());
                    return false;
                }
            }

            if (wait_.compare_exchange_weak(wait, wait + write1,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
                break;
        }

        FakeLock lock;
        Entry entry;
        entry.id = GetCurrentCoroID();
        entry.value = t;
        auto cond = [&](size_t size) -> typename cond_t::CondRet {
            typename cond_t::CondRet ret{true, true};
            if (closed_) {
                ret.canQueue = false;
                return ret;
            }

            if (size < capacity_) {
                ret.needWait = false;
                DebugPrint(dbg_channel, "[id=%ld] Push no wait.", this->getId());
                return ret;
            }

            if (!bWait) {
                ret.canQueue = false;
                return ret;
            }

            DebugPrint(dbg_channel, "[id=%ld] Push wait.", this->getId());
            return ret;
        };
        typename cond_t::cv_status cv_status;
        if (deadline == time_point_t())
            cv_status = wq_.wait(lock, entry, cond);
        else
            cv_status = wq_.wait_util(lock, deadline, entry, cond);

        switch ((int)cv_status) {
            case (int)cond_t::cv_status::no_timeout:
                if (closed_) {
                    DebugPrint(dbg_channel, "[id=%ld] Push failed by closed.", this->getId());
                    return false;
                }

                DebugPrint(dbg_channel, "[id=%ld] Push complete.", this->getId());
                return true;

            case (int)cond_t::cv_status::timeout:
                DebugPrint(dbg_channel, "[id=%ld] Push timeout.", this->getId());
                wait_ -= write1;
                return false;

            case (int)cond_t::cv_status::no_queued:
                if (closed_)
                    DebugPrint(dbg_channel, "[id=%ld] Push failed by closed.", this->getId());
                else if (!bWait)
                    DebugPrint(dbg_channel, "[id=%ld] TryPush failed.", this->getId());
                else
                    DebugPrint(dbg_channel, "[id=%ld] Push failed.", this->getId());
                wait_ -= write1;
                return false;

            default:
                assert(false);
                return false;
        }

        return false;
    }

    // read
    bool Pop(T & t, bool bWait, FastSteadyClock::time_point deadline = FastSteadyClock::time_point{})
    {
        DebugPrint(dbg_channel, "[id=%ld] Pop ->", this->getId());

        int spin = 0;

        uint64_t wait;
        for (;;) {
            wait = wait_.load(std::memory_order_relaxed);
            if (wait & writeMask) {

                if (!wq_.notify_one(
                            [&](Entry & entry)
                            {
                                t = entry.value;
                            }))
                {
                    if (++spin >= kSpinCount) {
                        spin = 0;
                        Processer::StaticCoYield();
                    }

                    if (closed_) {
                        DebugPrint(dbg_channel, "[id=%ld] Pop failed by closed.", this->getId());
                        return false;
                    } else if (!bWait) {
                        DebugPrint(dbg_channel, "[id=%ld] TryPop failed.", this->getId());
                        return false;
                    }
                    continue;
                }

                DebugPrint(dbg_channel, "[id=%ld] Pop Notify.", this->getId());

                wait_ -= write1;
                return true;
            } else {
                if (closed_) {
                    DebugPrint(dbg_channel, "[id=%ld] Pop failed by closed.", this->getId());
                    return false;
                } else if (!bWait) {
                    DebugPrint(dbg_channel, "[id=%ld] TryPop failed.", this->getId());
                    return false;
                }
            }

            if (wait_.compare_exchange_weak(wait, wait + read1,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                break;
        }

        FakeLock lock;
        Entry entry;
        entry.id = GetCurrentCoroID();
        entry.pvalue = &t;
        auto cond = [&](size_t size) -> typename cond_t::CondRet {
            typename cond_t::CondRet ret{true, true};
            if (closed_) {
                ret.canQueue = false;
                return ret;
            }

            DebugPrint(dbg_channel, "[id=%ld] Pop wait.", this->getId());
            return ret;
        };
        typename cond_t::cv_status cv_status;
        if (deadline == time_point_t())
            cv_status = rq_.wait(lock, entry, cond);
        else
            cv_status = rq_.wait_util(lock, deadline, entry, cond);

        switch ((int)cv_status) {
            case (int)cond_t::cv_status::no_timeout:
                if (closed_) {
                    DebugPrint(dbg_channel, "[id=%ld] Pop failed by closed.", this->getId());
                    return false;
                }

                DebugPrint(dbg_channel, "[id=%ld] Pop complete.", this->getId());
                return true;

            case (int)cond_t::cv_status::timeout:
                DebugPrint(dbg_channel, "[id=%ld] Pop timeout.", this->getId());
                wait_ -= read1;
                return false;

            case (int)cond_t::cv_status::no_queued:
                if (closed_)
                    DebugPrint(dbg_channel, "[id=%ld] Pop failed by closed.", this->getId());
                else
                    DebugPrint(dbg_channel, "[id=%ld] Pop failed.", this->getId());
                wait_ -= read1;
                return false;

            default:
                assert(false);
                return false;
        }

        return false;
    }

    ~CASChannelImpl() {
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel destory.", this->getId());
    }

    void SetDbgMask(uint64_t mask) {
        dbg_mask_ = mask;
    }

    bool Empty()
    {
        return wq_.empty();
    }

    std::size_t Size()
    {
        return std::min<size_t>(capacity_, wq_.size());
    }

    void Close()
    {
        if (closed_) return ;

        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel Closed. size=%d", this->getId(), (int)Size());

        closed_ = true;
        rq_.notify_all();
        wq_.notify_all();
    }
};

} //namespace co
