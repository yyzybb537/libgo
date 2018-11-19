#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include <queue>
#include "co_condition_variable.h"

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
