#pragma once
#include <chrono>
#include <mutex>
#include <memory>
#include <functional>
#include <condition_variable>
#include "linked_skiplist.h"

namespace libgo
{

template <typename MutexT, typename ConditionVariableT>
class RoutineSyncTimerT
{
public:
    RoutineSyncTimerT() {}
    RoutineSyncTimerT(RoutineSyncTimerT const&) = delete;
    RoutineSyncTimerT& operator=(RoutineSyncTimerT const&) = delete;

    virtual ~RoutineSyncTimerT() {
        stop();
    }

    virtual void stop()
    {
        std::unique_lock<MutexT> lock(mtx_);
        if (exit_) {
            return ;
        }

        exit_ = true;
        cv_.notify_one();
    }

    typedef std::function<void()> func_type;
    typedef std::chrono::steady_clock clock_type;

    struct FuncWrapper
    {
        FuncWrapper() : mtx_(std::make_shared<MutexT>()) {}

        void set(func_type const& fn)
        {
            fn_ = fn;
            canceled_.store(false, std::memory_order_release);
            reset();
        }

        void reset()
        {
            done_ = false;
        }

        std::shared_ptr<MutexT> mutex() { return mtx_; }

        bool invoke()
        {
            if (canceled_.load(std::memory_order_acquire))
                return false;

            done_ = true;   // 先设置done, 为了reschedule中能重置状态
            try {
                fn_();
            } catch (...) {}
            return true;
        }

        void cancel()
        {
            canceled_.store(false, std::memory_order_release);
        }

        bool done()
        {
            return done_;
        }

    private:
        friend class RoutineSyncTimerT;
        func_type fn_;
        std::shared_ptr<MutexT> mtx_;
        std::atomic_bool canceled_ {false};
        bool done_ {false};
    };

    typedef LinkedSkipList<clock_type::time_point, FuncWrapper> container_type;
    typedef typename container_type::Node TimerId;

    inline static clock_type::time_point* null_tp() { return nullptr; }

    inline static clock_type::time_point now() { return clock_type::now(); }

    inline static std::chrono::milliseconds& loop_interval() {
        static std::chrono::milliseconds interval(20);
        return interval;
    }

    template<typename _Clock, typename _Duration>
    void schedule(TimerId & id,
            const std::chrono::time_point<_Clock, _Duration> & abstime,
            func_type const& fn)
    {
        clock_type::time_point tp = convert(abstime);
        id.key = tp;
        id.value.set(fn);
        orderedList_.buildNode(&id);

        std::unique_lock<MutexT> lock(mtx_);
        insert(id);
    }

    // 向后延期重新执行 (只能在定时回调函数中使用)
    template<typename _Clock, typename _Duration>
    void reschedule(TimerId & id, const std::chrono::time_point<_Clock, _Duration> & abstime)
    {
        clock_type::time_point tp = convert(abstime);
        id.key = tp;
        id.value.reset();
        
        std::unique_lock<MutexT> lock(mtx_);
        insert(id);
    }

    bool join_unschedule(TimerId & id)
    {
        std::shared_ptr<MutexT> invoke_mtx = id.value.mutex();

        std::unique_lock<MutexT> invoke_lock(*invoke_mtx); // ABBA
        id.value.cancel();

        std::unique_lock<MutexT> lock(mtx_);
        orderedList_.erase(&id);

        return id.value.done();
    }

public:
    void run()
    {
        std::unique_lock<MutexT> lock(mtx_);
        while (!exit_)
        {
            TimerId* id = orderedList_.front();
            auto nowTp = now();
            if (id && nowTp >= id->key) {
                std::shared_ptr<MutexT> invoke_mtx = id->value.mutex();
                std::unique_lock<MutexT> invoke_lock(*invoke_mtx, std::defer_lock);
                bool locked = invoke_lock.try_lock();   // ABBA

                orderedList_.erase(id);

                if (locked) {
                    lock.unlock();

                    id->value.invoke();

                    lock.lock();
                }

                continue;
            }

            std::chrono::milliseconds sleepTime(1);
            if (id) {
                std::chrono::milliseconds delta = std::chrono::duration_cast<
                    std::chrono::milliseconds>(id->key - nowTp);
                sleepTime = (std::min)(sleepTime, delta);
            } else {
                sleepTime = loop_interval();
            }

            nextCheckAbstime_ = std::chrono::duration_cast<std::chrono::nanoseconds>((now() + sleepTime).time_since_epoch()).count();

            cv_.wait_for(lock, sleepTime);
        }
    }

private:
    void insert(TimerId & id)
    {
        TimerId* front = orderedList_.front();
        orderedList_.insert(&id);
        if (&id == orderedList_.front())
        {
            if (!front || (front && id.key < front->key))
            {
                if (std::chrono::duration_cast<std::chrono::nanoseconds>(
                    (id.key).time_since_epoch()).count() < nextCheckAbstime_)
                {
                    cv_.notify_one();
                }
            }
        }
    }

    template<typename _Clock, typename _Duration>
    clock_type::time_point convert(const std::chrono::time_point<_Clock, _Duration> & abstime)
    {
        // DR 887 - Sync unknown clock to known clock.
        const typename _Clock::time_point c = _Clock::now();
        const clock_type::time_point s = clock_type::now();
        const auto delta = abstime - c;
        return s + delta;
    }

    clock_type::time_point convert(const clock_type::time_point & abstime)
    {
        return abstime;
    }

private:
    MutexT mtx_;
    container_type orderedList_;
    ConditionVariableT cv_;
    bool exit_ {false};
    int64_t nextCheckAbstime_ = 0;
};

class RoutineSyncTimer : public RoutineSyncTimerT<std::mutex, std::condition_variable>
{
public:
    typedef RoutineSyncTimerT<std::mutex, std::condition_variable> base_t;
    typedef base_t::func_type func_type;
    typedef base_t::clock_type clock_type;
    typedef base_t::TimerId TimerId;

    static RoutineSyncTimer& getInstance() {
        static RoutineSyncTimer * obj = new RoutineSyncTimer;
        return *obj;
    }

    RoutineSyncTimer() : thread_([this]{ run(); }) {}

    virtual void stop() {
        base_t::stop();
        if (thread_.joinable())
            thread_.join();
    }

private:
    std::thread thread_;
};

} // namespace libgo
