#pragma once
#include "../routine_sync/switcher.h"
#include "../scheduler/processer.h"

namespace co 
{

// libgo routine切换器
// routine_sync套件需要针对每一种routine自定义一个switcher类。
struct LibgoSwitcher : public libgo::RoutineSwitcherI
{
public:
    // 把当前routine标记为休眠状态, 但不立即休眠routine
    // 如下两种执行顺序都必须支持:
    //  mark -> sleep -> wake
    //  mark -> wake -> sleep
    virtual void mark() override {
        entry_ = Processer::Suspend();
    }

    // 在routine中调用的接口，用于休眠当前routine
    virtual void sleep() override {
        Processer::StaticCoYield();
    }

    // 在其他routine中调用，用于唤醒休眠的routine
    // @return: 返回唤醒成功or失败
    // @要求: 一次sleep多次wake，只有其中一次wake成功，并且其他wake不会产生副作用
    virtual bool wake() override {
        return Processer::Wakeup(entry_);
    }

    // 判断是否在协程中 (子类switcher必须实现这个接口)
    static bool isInRoutine() {
        return Processer::IsCoroutine();
    }

    // 返回协程私有变量的switcher (子类switcher必须实现这个接口)
    static libgo::RoutineSwitcherI & clsRef() {
        return static_cast<libgo::RoutineSwitcherI &>(
                *(LibgoSwitcher*)Processer::GetCurrentTask()->extern_switcher_);
    }

private:
    Processer::SuspendEntry entry_;
};

} // namespace co

