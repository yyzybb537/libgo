#pragma once
#include "../common/config.h"

#if USE_ROUTINE_SYNC

# include "../routine_sync/shared_mutex.h"

namespace co
{

/// 读写锁
class CoRWMutex : public libgo::SharedMutex
{
public:
    explicit CoRWMutex(bool writePriority = true) {
        (void)writePriority;
    }
    ~CoRWMutex() {
    }

    void RLock() {
        lock_shared();
    }
    bool RTryLock() {
        return try_lock_shared();
    }
    void RUnlock() {
        unlock_shared();
    }

    void WLock() {
        lock();
    }
    bool WTryLock() {
        return try_lock();
    }
    void WUnlock() {
        unlock();
    }

    bool IsLock() {
        return !is_idle();
    }

public:
    class ReadView
    {
    public:
        void lock() {
            self()->lock_shared();
        }
        bool try_lock() {
            return self()->try_lock_shared();
        }
        bool is_lock() {
            return self()->is_lock_shared();
        }
        void unlock() {
            self()->unlock_shared();
        }

        ReadView() = default;
        ReadView(ReadView const&) = delete;
        ReadView& operator=(ReadView const&) = delete;

    private:
        inline CoRWMutex* self() {
            return reinterpret_cast<CoRWMutex*>((void*)this);
        }
    };

    class WriteView
    {
    public:
        void lock() {
            self()->lock();
        }
        bool try_lock() {
            return self()->try_lock();
        }
        bool is_lock() {
            return self()->is_lock();
        }
        void unlock() {
            self()->unlock();
        }

        WriteView() = default;
        WriteView(WriteView const&) = delete;
        WriteView& operator=(WriteView const&) = delete;

    private:
        inline CoRWMutex* self() {
            return reinterpret_cast<CoRWMutex*>((void*)this);
        }
    };

    ReadView& Reader() {
        return *reinterpret_cast<ReadView*>((void*)this);
    }
    WriteView& Writer() {
        return *reinterpret_cast<WriteView*>((void*)this);
    }

    // 兼容旧版接口
    ReadView& reader() {
        return *reinterpret_cast<ReadView*>((void*)this);
    }
    WriteView& writer() {
        return *reinterpret_cast<WriteView*>((void*)this);
    }
};

typedef CoRWMutex co_rwmutex;
typedef CoRWMutex::ReadView co_rmutex;
typedef CoRWMutex::WriteView co_wmutex;

} //namespace co

#else

# include "../scheduler/processer.h"
# include <queue>
# include "co_condition_variable.h"

namespace co
{

/// 读写锁
class CoRWMutex
{
    LFLock lock_;
    long lockState_;  // 0:无锁, >=1:读锁, -1:写锁

    // 兼容原生线程
    ConditionVariableAny rCv_;
    ConditionVariableAny wCv_;

    // 是否写优先
    bool writePriority_;

public:
    explicit CoRWMutex(bool writePriority = true);
    ~CoRWMutex();

    void RLock();
    bool RTryLock();
    void RUnlock();

    void WLock();
    bool WTryLock();
    void WUnlock();

    bool IsLock();

private:
    void TryWakeUp();

public:
    class ReadView
    {
        friend class CoRWMutex;
        CoRWMutex * self_;

    public:
        void lock();
        bool try_lock();
        bool is_lock();
        void unlock();

        ReadView() = default;
        ReadView(ReadView const&) = delete;
        ReadView& operator=(ReadView const&) = delete;
    };

    class WriteView
    {
        friend class CoRWMutex;
        CoRWMutex * self_;

    public:
        void lock();
        bool try_lock();
        bool is_lock();
        void unlock();

        WriteView() = default;
        WriteView(WriteView const&) = delete;
        WriteView& operator=(WriteView const&) = delete;
    };

    ReadView& Reader();
    WriteView& Writer();

    // 兼容旧版接口
    ReadView& reader();
    WriteView& writer();

private:
    ReadView readView_;
    WriteView writeView_;
};

typedef CoRWMutex co_rwmutex;
typedef CoRWMutex::ReadView co_rmutex;
typedef CoRWMutex::WriteView co_wmutex;

} //namespace co
#endif //USE_ROUTINE_SYNC

