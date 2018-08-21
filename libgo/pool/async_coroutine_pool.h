#pragma once
#include "../common/config.h"
#include "../common/spinlock.h"
#include "../sync/channel.h"
#include "../scheduler/scheduler.h"

namespace co {

// 协程池
// 可以无缝与异步代码结合, 处理异步框架中的阻塞事件
class AsyncCoroutinePool
{
public:
    static AsyncCoroutinePool* Create(size_t maxCallbackPoints = 128);

    typedef std::function<void()> Func;

    // 初始化协程数量
    void InitCoroutinePool(size_t maxCoroutineCount);

    // 启动协程池 
    void Start(int minThreadNumber, int maxThreadNumber = 0);

    void Post(Func const& func, Func const& callback);

    template <typename R>
    void Post(Channel<R> const& ret, std::function<R()> const& func) {
        Post([=]{ ret << func(); }, NULL);
    }

    void Post(Channel<void> const& ret, std::function<void()> const& func) {
        Post([=]{ func(); ret << nullptr; }, NULL);
    }

    template <typename R>
    void Post(std::function<R()> const& func, std::function<void(R&)> const& callback) {
        std::shared_ptr<R> ctx(new R);
        Post([=]{ *ctx = func(); }, [=]{ callback(*ctx); });
    }

    // 触发点
    struct CallbackPoint
    {
        size_t Run(size_t maxTrigger = 0);

        void SetNotifyFunc(Func const& notify);

    private:
        friend class AsyncCoroutinePool;

        void Post(Func && cb);

        void Notify();

    private:
        Channel<Func> channel_;
        Func notify_;
    };

    // 绑定回调函数触发点, 可以绑定多个触发点, 轮流使用.
    // 如果不绑定触发点, 则回调函数直接在协程池的工作线程中触发.
    // 线程安全接口
    bool AddCallbackPoint(CallbackPoint * point);

private:
    AsyncCoroutinePool(size_t maxCallbackPoints);
    ~AsyncCoroutinePool() {}

    void Go();

    struct PoolTask {
        Func func_;
        Func cb_;
    };

private:
    size_t maxCoroutineCount_;
    std::atomic<int> coroutineCount_{0};
    Scheduler* scheduler_;
    Channel<PoolTask> tasks_;
    std::atomic<size_t> pointsCount_{0};
    std::atomic<size_t> writePointsCount_{0};
    size_t maxCallbackPoints_;
    std::atomic<size_t> robin_{0};
    CallbackPoint ** points_;
    LFLock started_;
};

} // namespace co
