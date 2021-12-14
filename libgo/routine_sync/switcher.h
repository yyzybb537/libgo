#pragma once
#include <memory>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include "linked_list.h"
#include "debug.h"

// # include <unistd.h>
// # include <sys/syscall.h>

namespace libgo
{


// 初始化函数 (用户需要打开namespace自行实现这个初始化函数)
// 在这个函数体内执行 RoutineSyncPolicy::registerSwitchers函数
extern void routine_sync_init_callback();

// routine切换器
// routine_sync套件需要针对每一种routine自定义一个switcher类。
struct RoutineSwitcherI
{
public:
    virtual ~RoutineSwitcherI() { 
        valid_ = false; 
    }

    // 把当前routine标记为休眠状态, 但不立即休眠routine
    // 如下两种执行顺序都必须支持:
    //  mark -> sleep -> wake(其他线程执行)
    //  mark -> wake(其他线程执行) -> sleep
    virtual void mark() = 0;

    // 在routine中调用的接口，用于休眠当前routine
    // sleep函数中切换routine执行权，当routine被重新唤醒时函数才返回
    virtual void sleep() = 0;

    // 在其他routine中调用，用于唤醒休眠的routine
    // @return: 返回唤醒成功or失败
    // @要求: 一次sleep多次wake，只有其中一次wake成功，并且其他wake不会产生副作用
    virtual bool wake() = 0;

    // 判断是否在协程中 (子类switcher必须实现这个接口)
    //static bool isInRoutine();

    // 返回协程私有变量的switcher (子类switcher必须实现这个接口)
    //static RoutineSwitcherI & clsRef()

private:
    bool valid_ = true;

public:
    inline bool valid() const { return valid_; }
};

struct PThreadSwitcher : public RoutineSwitcherI
{
public:
    PThreadSwitcher() {
        // printf("PThreadSwitcher threadid=%d\n", (int)syscall(SYS_gettid));
    }

    virtual ~PThreadSwitcher() {
        // printf("~PThreadSwitcher threadid=%d\n", (int)syscall(SYS_gettid));
    }

    virtual void mark() override
    {
        std::unique_lock<std::mutex> lock(mtx_);
        waiting_ = true;
    }

    virtual void sleep() override
    {
        std::unique_lock<std::mutex> lock(mtx_);
        while (waiting_)
            cond_.wait(lock);
    }

    virtual bool wake() override
    {
        std::unique_lock<std::mutex> lock(mtx_);
        if (!waiting_)
            return false;
            
        waiting_ = false;
        cond_.notify_one();
        return true;
    }

    static bool isInRoutine() { return true; }

    static RoutineSwitcherI & clsRef()
    {
        static thread_local PThreadSwitcher pts;
        return static_cast<RoutineSwitcherI &>(pts);
    }

private:
    std::mutex mtx_;
    std::condition_variable cond_;
    bool waiting_ = false;
};

// 配置器
struct RoutineSyncPolicy
{
public:
    // 注册switchers
    template <typename ... Switchers>
    static bool registerSwitchers(int overlappedLevel = 1) {
        if (overlappedLevel <= refOverlappedLevel())
            return false;

        refOverlappedLevel() = overlappedLevel;
        clsRefFunction() = &RoutineSyncPolicy::clsRef_T<Switchers...>;
        isInPThreadFunction() = &RoutineSyncPolicy::isInPThread_T<Switchers...>;
        return true;
    }

    static RoutineSwitcherI& clsRef()
    {
        static bool dummy = (routine_sync_init_callback(), true);
        (void)dummy;
        return clsRefFunction()();
    }

    static bool isInPThread()
    {
        static bool dummy = (routine_sync_init_callback(), true);
        (void)dummy;
        return isInPThreadFunction()();
    }

private:
    typedef std::function<RoutineSwitcherI& ()> ClsRefFunction;
    typedef std::function<bool()> IsInPThreadFunction;

    static int& refOverlappedLevel() {
        static int lv = -1;
        return lv;
    }

    static ClsRefFunction & clsRefFunction() {
        static ClsRefFunction fn;
        return fn;
    }

    static IsInPThreadFunction & isInPThreadFunction() {
        static IsInPThreadFunction fn;
        return fn;
    }

    template <typename S1, typename S2, typename ... Switchers>
    inline static RoutineSwitcherI& clsRef_T() {
        if (S1::isInRoutine()) {
            return S1::clsRef();
        }

        return clsRef_T<S2, Switchers...>();
    }

    template <typename S1>
    inline static RoutineSwitcherI& clsRef_T() {
        if (S1::isInRoutine()) {
            return S1::clsRef();
        }

        return PThreadSwitcher::clsRef();
    }

    template <typename S1, typename S2, typename ... Switchers>
    inline static bool isInPThread_T() {
        if (S1::isInRoutine()) {
            return false;
        }

        return isInPThread_T<S2, Switchers...>();
    }

    template <typename S1>
    inline static bool isInPThread_T() {
        if (S1::isInRoutine()) {
            return false;
        }

        return true;
    }
};

} // namespace libgo
