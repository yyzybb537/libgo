#pragma once
#include <functional>
#include "channel.h"
#include "ts_queue.h"

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
    std::atomic<uint8_t> sleep_ms_{0};

public:
    ~ThreadPool();

    uint32_t Run();
    
    void RunLoop();

    template <typename R>
    void AsyncWait(Channel<R> const& ch, std::function<R()> const& fn)
    {
        TPElemBase *elem = new TPElem<R>(ch, fn);
        this->elem_list_.push(elem);
    }
};

} //namespace co
