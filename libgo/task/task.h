#pragma once
#include "../common/config.h"
#include "../common/ts_queue.h"
#include "../common/anys.h"
#include "../context/context.h"
#include "../debug/debugger.h"
#include "../common/lock_free_ring_history.h"

namespace co
{

enum class TaskState
{
    runnable,
    block,
    done,
};

const char* GetTaskStateName(TaskState state);

enum class SchedulerEvent
{
    uninitialized = 0,
    fatal,

    swapIn,
    swapOut_runnable,
    swapOut_block,
    swapOut_done,

    suspend,
    wakeup,

    addIntoNewQueue,
    addIntoRunnableQueue,
    addIntoWaitQueue,
    addIntoStealList,
    popRunnableQueue,
    pushRunnableQueue,
};

const char* GetSchedulerEventName(SchedulerEvent event);


typedef std::function<void()> TaskF;

struct TaskGroupKey {};
typedef Anys<TaskGroupKey> TaskAnys;

class Processer;

struct Task
    : public TSQueueHook, public SharedRefObject, public CoDebugger::DebuggerBase<Task>
{
    TaskState state_ = TaskState::runnable;
    uint64_t id_;
    Processer* proc_ = nullptr;
    Context ctx_;
    TaskF fn_;
    std::exception_ptr eptr_;           // 保存exception的指针
    TaskAnys anys_;

    LFLock debugLock_;
    LockFreeRingHistory<std::pair<SchedulerEvent, int64_t>> stateHistory_;

    uint64_t yieldCount_ = 0;

    Task(TaskF const& fn, std::size_t stack_size);
    ~Task();

    void UpdateSchedulerEvent(SchedulerEvent event, int threadId) {
        stateHistory_.Push(std::pair<SchedulerEvent, int64_t>(event, threadId));
    }

    ALWAYS_INLINE void SwapIn()
    {
//        assert(debugLock_.try_lock());
        if (!debugLock_.try_lock()) {
            UpdateSchedulerEvent(SchedulerEvent::fatal, GetCurrentThreadID());

            std::string stateHistories;
            auto vecStates = stateHistory_.PopAll();
            for (auto & s : vecStates) {
                char buf[128] = {};
                snprintf(buf, sizeof(buf), "%s %ld\n", GetSchedulerEventName(s.first), s.second);
                stateHistories += buf;
            }
            printf("%s\n", stateHistories.c_str());

            assert(false);
        }

        ctx_.SwapIn();
    }
    ALWAYS_INLINE void SwapTo(Task* other)
    {
        ctx_.SwapTo(other->ctx_);
    }
    ALWAYS_INLINE void SwapOut()
    {
        assert(!debugLock_.try_lock());
        assert((debugLock_.unlock(), true));
        ctx_.SwapOut();
    }

    const char* DebugInfo();

private:
    void Run();

    static void StaticRun(intptr_t vp);

    Task(Task const&) = delete;
    Task(Task &&) = delete;
    Task& operator=(Task const&) = delete;
    Task& operator=(Task &&) = delete;
};

#define TaskInitPtr reinterpret_cast<Task*>(0x1)
#define TaskRefDefine(type, name) \
    ALWAYS_INLINE type& TaskRef ## name(Task *tk) \
    { \
        typedef type T; \
        static int idx = -1; \
        if (UNLIKELY(tk == TaskInitPtr)) { \
            if (idx == -1) \
                idx = TaskAnys::Register<T>(); \
            static T ignore{}; \
            return ignore; \
        } \
        return tk->anys_.get<T>(idx); \
    }
#define TaskRefInit(name) do { TaskRef ## name(TaskInitPtr); } while(0)

} //namespace co
