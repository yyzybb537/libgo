#pragma once
#include "config.h"
#include <chrono>
#include "clock.h"
#include "ts_queue.h"
#include "spinlock.h"
#include "util.h"

namespace co
{

template <typename F>
class Timer
{
public:
    struct Element : public TSQueueHook, public RefObject , public ObjectCounter<Element>
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

        // 切换齿轮时, 如果isValid == false, 则直接DecrementRef
        inline bool isValid() { return !active_.is_lock(); }
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

    TimerId StartTimer(FastSteadyClock::duration dur, F const& cb);
    
    void ThreadRun();

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
    Element* element = NewElement();
    element->init(cb, FastSteadyClock::now() + dur);
    TimerId timerId(element);

    Dispatch(element, false);
    return timerId;
}

    template <typename F>
void Timer<F>::ThreadRun()
{
    for (;;) {
        Trigger(completeSlot_);

        auto now = FastSteadyClock::now();

        uint64_t durVal = std::chrono::duration_cast<FastSteadyClock::duration>(
                now - begin_).count() / precision_.count();
        Point & p = *(Point*)&durVal;

        // 先步进point_. (此处为ABBA线程同步)
        // A: 步进point_ (volatile)
        // B: slot.lock
        Point last;
        last.p64 = point_.p64;
        point_.p64 = p.p64;

        // 寻找此次需要转动的最大刻度
        int topLevel = 0;
        for (int i = 7; i >= 0; --i) {
            if (p.p8[i] > last.p8[i]) {
                topLevel = i;
                break;
            }
        }

        //        printf("p %lu -> %lu. topLevel=%d\n", last.p64, p.p64, topLevel);

        // 低于此刻度的, 都可以直接trigger了
        for (int i = 0; i < topLevel; ++i) {
            for (auto & slot : slots_[i])
                Trigger(slot);
        }

        // 已经划过的顶级刻度
        for (int i = last.p8[topLevel]; i < p.p8[topLevel]; ++i) {
            Trigger(slots_[topLevel][i]);
        }

        // 拆分最后一个顶级刻度
        if (topLevel > 0) {
            Slot & slot = slots_[topLevel][p.p8[topLevel]];
            Dispatch(slot, now);
        }

        std::this_thread::sleep_for(precision_);
        //        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

    template <typename F>
void Timer<F>::Trigger(Slot & slot)
{
    SList<Element> slist = slot.pop_all();
    for (Element & element : slist) {
        slist.erase(&element);
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
    uint64_t durVal = std::chrono::duration_cast<FastSteadyClock::duration>(
            element->tp_ - begin_).count() / precision_.count();
    if (!mainloop && durVal < point_.p64) {
        completeSlot_.push(element);
        return ;
    }

    Point & p = *(Point*)&durVal;
    int level = 0;
    int offset = 0;
    for (int i = 7; i >= 0; --i) {
        if (p.p8[i] > 0) {
            level = i;
            offset = p.p8[i];
            break;
        }
    }

    auto & wheel = slots_[level];
    auto & slot = wheel[offset];

    std::unique_lock<typename Slot::lock_t> lock(slot.LockRef());
    if (!mainloop && durVal < point_.p64) {
        lock.unlock();
        completeSlot_.push(element);
        return ;
    }
    slot.pushWithoutLock(element);
    element->slot_ = &slot;
    return ;
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

} // namespace co
