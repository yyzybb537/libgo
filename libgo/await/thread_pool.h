#pragma once
#include <functional>
#include <condition_variable>
#include <mutex>
#include <libgo/channel.h>
#include <libgo/ts_queue.h>

namespace co {

struct TPElemBase : public TSQueueHook
{
    virtual ~TPElemBase() {}
    virtual void Do() = 0;
};

template <typename R>
struct TPElem;

template <>
struct TPElem<void> : TPElemBase
{
    Channel<void> ch_;
    std::function<void()> fn_;

    TPElem(Channel<void> const& ch, std::function<void()> const& fn)
        : ch_(ch), fn_(fn) {}

    virtual void Do()
    {
        if (ch_.Unique()) return ;
        fn_();
        ch_ << nullptr;
    }
};

template <typename R>
struct TPElem : TPElemBase
{
    Channel<R> ch_;
    std::function<R()> fn_;

    TPElem(Channel<R> const& ch, std::function<R()> const& fn)
        : ch_(ch), fn_(fn) {}

    virtual void Do()
    {
        if (ch_.Unique()) return ;
        ch_ << fn_();
    }
};

class ThreadPool
{
    typedef TSQueue<TPElemBase> ElemList;
    ElemList elem_list_;
    std::mutex cond_mtx_;
    std::condition_variable cond_;

public:
    ~ThreadPool();
    
    void RunLoop();

    template <typename R>
    void AsyncWait(Channel<R> const& ch, std::function<R()> const& fn)
    {
        assert_std_thread_lib();

        TPElemBase *elem = new TPElem<R>(ch, fn);
        std::unique_lock<std::mutex> lock(cond_mtx_);
        this->elem_list_.push(elem);
        cond_.notify_one();
    }

private:
    TPElemBase* get();

    void assert_std_thread_lib();

    bool __assert_std_thread_lib();
};

} //namespace co

