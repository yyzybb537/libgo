#pragma once
#include "config.h"
#include <chrono>
#include "clock.h"
#include "ts_queue.h"
#include "spinlock.h"
#include "util.h"
#include "dbg_timer.h"
#include <condition_variable>

namespace co
{

template <typename F>
class Timer : public IdCounter<Timer<F>>
{
public:
    struct Element : public TSQueueHook, public RefObject, public IdCounter<Element>
    {
        F cb_;
        LFLock active_;
        FastSteadyClock::time_point tp_;
        void* volatile slot_;

        inline void init(F const& cb, FastSteadyClock::time_point tp) {
            cb_ = cb;
            tp_ = tp;
            active_.try_lock();
            active_.unlock();
            slot_ = nullptr;
        }

        inline void call() noexcept {
            std::unique_lock<LFLock> lock(active_, std::defer_lock);
            if (!lock.try_lock()) return ;
            slot_ = nullptr;
            cb_();
        }

        // 删除时可能正在切换齿轮, 无法立即回收, 不过没关系, 下一次切换齿轮的时候会回收
        inline bool cancel() {
            if (!active_.try_lock()) return false;
            if (slot_) {
                ((TSQueue<Element>*)slot_)->erase(this);
            }
            this->DecrementRef();
            return true;
        }
    };
    typedef TSQueue<Element> Slot;
    typedef TSQueue<Element> Pool;

public:
    struct TimerId
    {
        TimerId() {}
        explicit TimerId(Element* elem) : elem_(elem) {}

        explicit operator bool() const {
            return !!elem_;
        }

        bool StopTimer() {
            bool ret = true;
            if (elem_) {
                ret = elem_->cancel();
            }

            elem_.reset();
            return ret;
        }

        friend class Timer;
    private:
        IncursivePtr<Element> elem_;
    };

public:
    Timer();

    template <typename Rep, typename Period>
    void SetPrecision(std::chrono::duration<Rep, Period> precision);

    void SetPoolSize(int max, int reserve = 0);

    std::size_t GetPoolSize();

    // 设置定时器
    TimerId StartTimer(FastSteadyClock::duration dur, F const& cb);
    TimerId StartTimer(FastSteadyClock::time_point tp, F const& cb);
    
    // 循环执行触发检查
    void ThreadRun();

    // 执行一次触发检查
    void RunOnce();

    // 检查下一次触发还需要多久
    FastSteadyClock::time_point NextTrigger(FastSteadyClock::duration max);

    std::string DebugInfo();
    // 设置结束标志位, 用于安全退出
    void Stop();

private:
    void Init();

    Element* NewElement();

    void DeleteElement(Element*);

    static void StaticDeleteElement(RefObject* ptr, void* arg);

    void Trigger(Slot & slot);

    void Dispatch(Slot & slot, FastSteadyClock::time_point now);

    // 将Element插入时间轮中
    // @mainloop: 是否在触发线程, 如果为true, 则无需检验是否需要加入completeSlot_.
    void Dispatch(Element * element, bool mainloop);

//private:
public:
    volatile bool stop_ = false;
    std::mutex quitMtx_;
    std::condition_variable_any quit_;

    int maxPoolSize_ = 0;
    Pool pool_;

    // 起始时间
    FastSteadyClock::time_point begin_;

    // 精度
    FastSteadyClock::duration precision_;

    // 齿轮
    Slot slots_[8][256];

    // 指针
    union Point {
        uint64_t p64;
        uint8_t p8[8];
    };
    volatile Point point_;

    // 需要立即执行的slot位
    Slot completeSlot_;
};

template <typename F>
Timer<F>::Timer()
{
    begin_ = FastSteadyClock::now();
    point_.p64 = 0;
    pool_.check_ = this;
    precision_ = std::chrono::microseconds(100);
//    precision_ = std::chrono::microseconds(1000);
}

template <typename F>
template <typename Rep, typename Period>
void Timer<F>::SetPrecision(std::chrono::duration<Rep, Period> precision)
{
    if (precision < std::chrono::microseconds(100))
        precision = std::chrono::microseconds(100);
    if (precision > std::chrono::minutes(1))
        precision = std::chrono::minutes(1);

    precision_ = std::chrono::duration_cast<FastSteadyClock::duration>(precision);
}

template <typename F>
void Timer<F>::SetPoolSize(int max, int reserve)
{
    maxPoolSize_ = max;

    if ((int)pool_.size() < reserve) {
        TSQueue<Element, false> reservePool;
        reservePool.check_ = pool_.check_;
        for (int i = pool_.size(); i < reserve; ++i) {
            auto ptr = new Element;
            ptr->SetDeleter(Deleter(&Timer<F>::StaticDeleteElement, (void*)this));
            reservePool.push(ptr);
        }
        pool_.push(reservePool.pop_all());
    }
}

template <typename F>
std::size_t Timer<F>::GetPoolSize()
{
    return pool_.size();
}

template <typename F>
typename Timer<F>::TimerId Timer<F>::StartTimer(FastSteadyClock::duration dur, F const& cb)
{
    return StartTimer(FastSteadyClock::now() + dur, cb);
}

template <typename F>
typename Timer<F>::TimerId Timer<F>::StartTimer(FastSteadyClock::time_point tp, F const& cb)
{
    Element* element = NewElement();
    element->init(cb, tp);
    TimerId timerId(element);

    Dispatch(element, false);
    return timerId;
}

template <typename F>
void Timer<F>::ThreadRun()
{
    while (!stop_) {
        RunOnce();

//        auto s = FastSteadyClock::now();
        std::this_thread::sleep_for(precision_);
//        auto e = FastSteadyClock::now();
//        DebugPrint(dbg_timer, "[id=%ld]Thread sleep %ld us", this->getId(),
//                std::chrono::duration_cast<std::chrono::microseconds>(e - s).count());
    }

    std::unique_lock<std::mutex> lock(quitMtx_);
    quit_.notify_one();
}

template <typename F>
void Timer<F>::Stop()
{
    stop_ = true;

    std::unique_lock<std::mutex> lock(quitMtx_);
    quit_.wait(lock);
}

template <typename F>
void Timer<F>::RunOnce()
{
    DbgTimer dt(dbg_timer);

    Trigger(completeSlot_);
    DBG_TIMER_CHECK(dt);

    auto now = FastSteadyClock::now();
    DBG_TIMER_CHECK(dt);

    uint64_t destVal = std::chrono::duration_cast<FastSteadyClock::duration>(
            now - begin_).count() / precision_.count();
    Point & destPoint = *(Point*)&destVal;
    if (destPoint.p64 <= point_.p64) return ;
    DBG_TIMER_CHECK(dt);

    Point delta;
    delta.p64 = destPoint.p64 - point_.p64;

    Point last;
    last.p64 = point_.p64;

#if LIBGO_SYS_Windows
    point_.p64 = destPoint.p64;
	std::atomic_thread_fence(std::memory_order_release);
#else
    __atomic_store(&point_.p64, &destPoint.p64, std::memory_order_release);
#endif

    DBG_TIMER_CHECK(dt);

    DebugPrint(dbg_timer, "[id=%ld]RunOnce point:<%d><%d><%d><%d> ----> <%d><%d><%d><%d>",
            this->getId(),
            (int)last.p8[0], (int)last.p8[1], (int)last.p8[2], (int)last.p8[3],
            (int)point_.p8[0], (int)point_.p8[1], (int)point_.p8[2], (int)point_.p8[3]
            );
    DBG_TIMER_CHECK(dt);

    // 未扫完的级别 
    int triggerLevel = 0;

    // 每级Wheel扫过了几个slot
    int triggerSlots[8] = {};

    const uint64_t additions[8] = {(uint64_t)1, (uint64_t)1 << 8, (uint64_t)1 << 16, (uint64_t)1 << 32, (uint64_t)1 << 40,
        (uint64_t)1 << 48, (uint64_t)1 << 56, (std::numeric_limits<uint64_t>::max)()};

    Point pos;

    Slot dispatchers;

    uint64_t i = 0;
    while (i < delta.p64) {
        pos.p64 = last.p64 + i;

        int lv = triggerLevel;
        int slotIdx = pos.p8[lv];
        
        DebugPrint(dbg_timer, "[id=%ld]RunOnce Trigger(i=%d) [L=%d][%d]", this->getId(), (int)i, lv, slotIdx);
        Trigger(slots_[lv][slotIdx]);
        if (++triggerSlots[lv] == 256)
            ++triggerLevel;

        while (pos.p64 > 0 && slotIdx == 0) {
            // 升级
            ++lv;
            slotIdx = pos.p8[lv];
            DebugPrint(dbg_timer, "[id=%ld]RunOnce Dispatch [L=%d][%d]", this->getId(), lv, slotIdx);
            dispatchers.push(slots_[lv][slotIdx].pop_all());
            ++triggerSlots[lv];
        }

        uint64_t add = additions[triggerLevel];
        if (triggerLevel > 0) {
            uint64_t levelupSteps = (256 - pos.p8[triggerLevel - 1]) * additions[triggerLevel - 1];
            if (levelupSteps > 0)
                add = (std::min)(levelupSteps, add);
        }

        i += add;
    }
    DBG_TIMER_CHECK(dt);

    Dispatch(dispatchers, now);
    DBG_TIMER_CHECK(dt);

    DebugPrint(dbg_timer, "[id=%ld]RunOnce Done. DbgTimer: %s", this->getId(), dt.ToString().c_str());
}

template <typename F>
FastSteadyClock::time_point Timer<F>::NextTrigger(FastSteadyClock::duration max)
{
    if (max.count() == 0) return FastSteadyClock::now();
    if (!completeSlot_.empty()) return FastSteadyClock::now();

    auto dest = FastSteadyClock::now() + max;
    uint64_t durVal = std::chrono::duration_cast<FastSteadyClock::duration>(
            dest - begin_).count() / precision_.count();
    Point & p = *(Point*)&durVal;
    Point last;

#if LIBGO_SYS_Windows
	std::atomic_thread_fence(std::memory_order_acquire);
	last.p64 = point_.p64;	
#else 
	__atomic_load(&point_.p64, &last.p64, std::memory_order_acquire);
#endif

    auto lastTime = last.p64 * precision_ + begin_;
    if (last.p64 >= p.p64) return FastSteadyClock::now();

    // 寻找此次需要检查的最大刻度
    int topLevel = 0;
    for (int i = 7; i >= 0; --i) {
        if (last.p8[i] < p.p8[i]) {
            topLevel = i;
            break;
        }
    }

    // 从小刻度到大刻度依次检查
    for (int i = 0; i < topLevel; ++i) {
        for (int k = 0; k < 256; ++k) {
            int j = (k + last.p8[i]) & 0xff;
            if (!slots_[i][j].empty()) {
                p.p8[i] = j;

                for (int m = i+1; m < 8; ++m) {
                    p.p8[m] = last.p8[m] = 0;
                }

                // 进位
                if (j < last.p8[i])
                    p.p8[i+1] = 1;

                goto check_done;
            }
        }
    }

    for (int j = last.p8[topLevel]; j < p.p8[topLevel]; ++j) {
        if (!slots_[topLevel][j].empty()) {
            p.p8[topLevel] = j;
            goto check_done;
        }
    }

check_done:
    if (p.p64 > last.p64) 
        return (p.p64 - last.p64) * precision_ + lastTime;

    return FastSteadyClock::now();
}

template <typename F>
void Timer<F>::Trigger(Slot & slot)
{
    SList<Element> slist = slot.pop_all();
    for (Element & element : slist) {
        slist.erase(&element);
        DebugPrint(dbg_timer, "[id=%ld]Timer trigger element=%ld precision= %d us",
                this->getId(), element.getId(),
                (int)std::chrono::duration_cast<std::chrono::microseconds>(FastSteadyClock::now() - element.tp_).count());
        element.call();
        element.DecrementRef();
    }
    slist.clear();
}

template <typename F>
void Timer<F>::Dispatch(Slot & slot, FastSteadyClock::time_point now)
{
    SList<Element> slist = slot.pop_all();
    for (Element & element : slist) {
        slist.erase(&element);
        if (element.tp_ <= now) {
            DebugPrint(dbg_timer, "[id=%ld]Timer trigger element=%ld precision= %d us",
                    this->getId(), element.getId(),
                    (int)std::chrono::duration_cast<std::chrono::microseconds>(FastSteadyClock::now() - element.tp_).count());
            element.call();
            element.DecrementRef();
        } else {
            Dispatch(&element, true);
        }
    }
    slist.clear();
}

template <typename F>
void Timer<F>::Dispatch(Element * element, bool mainloop)
{
sync_retry:
    Point last;
#if LIBGO_SYS_Windows
	std::atomic_thread_fence(std::memory_order_acquire);
    last.p64 = point_.p64;
#else
    __atomic_load(&point_.p64, &last.p64, std::memory_order_acquire);
#endif
    FastSteadyClock::time_point lastTime(begin_ + last.p64 * precision_);

    if (!mainloop && element->tp_ <= lastTime) {
        completeSlot_.push(element);

        DebugPrint(dbg_timer, "[id=%ld]Timer Dispatch mainloop=%d element=%ld into completeSlot",
                this->getId(), (int)mainloop, element->getId());
        return ;
    }

    uint64_t durNanos = std::chrono::duration_cast<FastSteadyClock::duration>(
            element->tp_ - lastTime).count();
    uint64_t durVal = durNanos / precision_.count();

    Point & p = *(Point*)&durVal;
    int level = 0;
    int offset = last.p8[0];
    for (int i = 7; i >= 0; --i) {
        if (p.p8[i] > 0) {
            level = i;
            offset = (p.p8[i] + last.p8[i]) & 0xff;
            break;
        }
    }

    auto & wheel = slots_[level];
    auto & slot = wheel[offset];

    {
        std::unique_lock<typename Slot::lock_t> lock(slot.LockRef());
        uint64_t atomicPointP64;
#if LIBGO_SYS_Windows
		std::atomic_thread_fence(std::memory_order_acquire);
		atomicPointP64 = point_.p64;
#else 
		__atomic_load(&point_.p64, &atomicPointP64, std::memory_order_acquire);
#endif
       
        if (last.p64 != atomicPointP64) {
            goto sync_retry;
        }

        slot.pushWithoutLock(element);
        element->slot_ = &slot;
    }

    DebugPrint(dbg_timer, "[id=%ld]Timer Dispatch mainloop=%d element=%ld durNanos=%ld point:<%d><%d><%d> slot:[L=%d][%d]",
            this->getId(), (int)mainloop, element->getId(), (long)durNanos,
            (int)last.p8[0], (int)last.p8[1], (int)last.p8[2],
            (int)level, (int)offset);
}

template <typename F>
typename Timer<F>::Element* Timer<F>::NewElement()
{
    Element *ptr = nullptr;
    if (!pool_.emptyUnsafe())
        ptr = pool_.pop();

    if (!ptr) {
        ptr = new Element;
        ptr->SetDeleter(Deleter(&Timer<F>::StaticDeleteElement, (void*)this));
    }

    return ptr;
}

template <typename F>
void Timer<F>::StaticDeleteElement(RefObject* ptr, void* arg)
{
    Timer* self = (Timer*)arg;
    self->DeleteElement((Element*)ptr);
}

template <typename F>
void Timer<F>::DeleteElement(Element* element)
{
    if ((int)pool_.size() < maxPoolSize_) {
        element->IncrementRef();
        element->cb_ = F();
        pool_.push(element);
    }
    else
        delete element;
}

template <typename F>
std::string Timer<F>::DebugInfo()
{
    std::string s;
    s += P("-------- Point: <%d><%d><%d><%d> ---------",
            (int)point_.p8[0], (int)point_.p8[1], (int)point_.p8[2], (int)point_.p8[3]);
    s += P("------------- Timer Tasks --------------");
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 256; ++j) {
            Slot & slot = slots_[i][j];
            std::size_t count = slot.size();
            if (count == 0) continue;
            s += P("slot[%d][%d] -> count = %d\n", i, j, (int)count);
        }
    }
    return s;
}

} // namespace co
