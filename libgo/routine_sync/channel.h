#pragma once
#include "condition_variable.h"
#include "timer.h"
#include <deque>
#include <exception>
#include <iomanip>
#include <type_traits>

namespace libgo
{

template <
    typename T,
    typename QueueT
>
class ChannelImpl;

template <
    typename T,
    typename QueueT = std::deque<T>
>
class Channel;

template <typename T>
class ChannelImplWithSignal : public DebuggerId<ChannelImplWithSignal<int>>
{
public:
    template<typename _Clock, typename _Duration>
    bool pop_impl_with_signal_noqueued(T & t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime,
            std::unique_lock<Mutex> & lock)
    {
        RS_DBG(dbg_channel, "channel=%ld | %s | ptr(t)=0x%p | isWait=%d | abstime=%d | closed=%d | popWaiting_=%p | pushWaiting_=%p",
            id(), __func__, (void*)&t, isWait, !!abstime, closed_, (void*)popWaiting_, (void*)pushWaiting_);

        if (closed_)
            return false;

        if (pushWaiting_) {   // 有push协程在等待, 读出来 & 清理
            t = *pushQ_;
            pushQ_ = nullptr;

            RS_DBG(dbg_channel, "channel=%ld | %s | match pop success | notify peer",
                id(), __func__);

            pushWaiting_->fast_notify_all(lock);
            pushWaiting_ = nullptr;

            pushCv_.notify_one();
            popCv_.notify_one();
            return true;
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel=%ld | %s | not match && not wait | return false",
                id(), __func__);
            return false;
        }

        // 开始等待
        T temp;
        ConditionVariable waiting;

        popQ_ = &temp;
        popWaiting_ = &waiting;

        RS_DBG(dbg_channel, "channel=%ld | %s | begin wait matched",
            id(), __func__);

        (void)waiting.wait_until_p(lock, abstime, [&]{ return popWaiting_ != &waiting; });
        bool ok = popWaiting_ != &waiting;

        RS_DBG(dbg_channel, "channel=%ld | %s | waked | matched=%d",
            id(), __func__, ok);

        if (ok) {
            // 成功
            t = std::move(temp);    // 对外部T的写操作放到本线程来做, 降低使用难度
        } else {
            // 超时，清理
            popQ_ = nullptr;
            popWaiting_ = nullptr;
        }
        return ok;
    }

    template<typename _Clock, typename _Duration>
    bool pop_impl_with_signal(T & t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        std::unique_lock<Mutex> lock(mtx_);

        RS_DBG(dbg_channel, "channel=%ld | %s | ptr(t)=0x%p | isWait=%d | abstime=%d | closed=%d | popWaiting_=%p | pushWaiting_=%p",
            id(), __func__, (void*)&t, isWait, !!abstime, closed_, (void*)popWaiting_, (void*)pushWaiting_);

        if (!popWaiting_) {
            return pop_impl_with_signal_noqueued(t, isWait, abstime, lock);
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel=%ld | %s | pop contended && not wait | return false",
                id(), __func__);
            return false;
        }

        // 有其他pop在等待, 进pop队列
        RS_DBG(dbg_channel, "channel=%ld | %s | begin wait at pop queued",
            id(), __func__);

        auto p = [this]{ return !popWaiting_; };
        while (!p()) {
            if (popCv_.wait_until_impl(lock, abstime) == std::cv_status::timeout) {
                RS_DBG(dbg_channel, "channel=%ld | %s | waked | pop busy | timeout",
                    id(), __func__);
                return false;
            }
        }

        RS_DBG(dbg_channel, "channel=%ld | %s | waked | pop idle",
            id(), __func__);
        return pop_impl_with_signal_noqueued(t, isWait, abstime, lock);
    }

    template<typename _Clock, typename _Duration>
    bool push_impl_with_signal_noqueued(T const& t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime,
            std::unique_lock<Mutex> & lock)
    {
        RS_DBG(dbg_channel, "channel=%ld | %s | ptr(t)=0x%p | isWait=%d | abstime=%d | closed=%d | popWaiting_=%p | pushWaiting_=%p",
            id(), __func__, (void*)&t, isWait, !!abstime, closed_, (void*)popWaiting_, (void*)pushWaiting_);

        if (closed_)
            return false;

        if (popWaiting_) {   // 有pop协程在等待, 写入 & 清理
            *popQ_ = t;
            popQ_ = nullptr;

            RS_DBG(dbg_channel, "channel=%ld | %s | match push success | notify peer",
                id(), __func__);

            popWaiting_->fast_notify_all(lock);
            popWaiting_ = nullptr;

            pushCv_.notify_one();
            popCv_.notify_one();
            return true;
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel=%ld | %s | not match && not wait | return false",
                id(), __func__);
            return false;
        }

        // 开始等待
        ConditionVariable waiting;

        pushQ_ = &t;
        pushWaiting_ = &waiting;

        RS_DBG(dbg_channel, "channel=%ld | %s | begin wait matched",
            id(), __func__);

        (void)waiting.wait_until_p(lock, abstime, [&]{ return pushWaiting_ != &waiting; });
        bool ok = pushWaiting_ != &waiting;

        RS_DBG(dbg_channel, "channel=%ld | %s | waked | matched=%d",
            id(), __func__, ok);

        if (ok) {
            // 成功
        } else {
            // 超时，清理
            pushQ_ = nullptr;
            pushWaiting_ = nullptr;
        }
        return ok;
    }

    template<typename _Clock, typename _Duration>
    bool push_impl_with_signal(T const& t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        std::unique_lock<Mutex> lock(mtx_);

        RS_DBG(dbg_channel, "channel=%ld | %s | ptr(t)=0x%p | isWait=%d | abstime=%d | closed=%d | popWaiting_=%p | pushWaiting_=%p",
            id(), __func__, (void*)&t, isWait, !!abstime, closed_, (void*)popWaiting_, (void*)pushWaiting_);

        if (!pushQ_) {
            return push_impl_with_signal_noqueued(t, isWait, abstime, lock);
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel=%ld | %s | push contended && not wait | return false",
                id(), __func__);
            return false;
        }

        // 有其他push在等待, 进push队列
        RS_DBG(dbg_channel, "channel=%ld | %s | begin wait at push queued",
            id(), __func__);

        auto p = [this]{ return !pushQ_; };
        while (!p()) {
            if (pushCv_.wait_until_impl(lock, abstime) == std::cv_status::timeout) {
                RS_DBG(dbg_channel, "channel=%ld | %s | waked | push busy | timeout",
                    id(), __func__);
                return false;
            }
        }

        RS_DBG(dbg_channel, "channel=%ld | %s | waked | push idle",
            id(), __func__);
        return push_impl_with_signal_noqueued(t, isWait, abstime, lock);
    }

protected:
    Mutex mtx_;
    ConditionVariable pushCv_;
    ConditionVariable popCv_;
    bool closed_ {false};

    T const* pushQ_ {nullptr};
    T* popQ_ {nullptr};
    ConditionVariable* pushWaiting_ {nullptr};
    ConditionVariable* popWaiting_ {nullptr};
};

template <
    typename T,
    typename QueueT
>
class ChannelImpl : public ChannelImplWithSignal<T>
{
public:
    using ChannelImplWithSignal<T>::mtx_;
    using ChannelImplWithSignal<T>::pushCv_;
    using ChannelImplWithSignal<T>::popCv_;
    using ChannelImplWithSignal<T>::closed_;
    using ChannelImplWithSignal<T>::pushQ_;
    using ChannelImplWithSignal<T>::popQ_;
    using ChannelImplWithSignal<T>::popWaiting_;
    using ChannelImplWithSignal<T>::pushWaiting_;
    using ChannelImplWithSignal<T>::pop_impl_with_signal;
    using ChannelImplWithSignal<T>::push_impl_with_signal;
    using ChannelImplWithSignal<T>::id;

    explicit ChannelImpl(std::size_t capacity = 0)
        : cap_(capacity)
    {
    }

    template<typename _Clock, typename _Duration>
    bool push(T const& t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        if (cap_) {
            return push_impl_with_cap(t, isWait, abstime);
        }

        return push_impl_with_signal(t, isWait, abstime);
    }

    template<typename _Clock, typename _Duration>
    bool pop(T & t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        if (cap_) {
            return pop_impl_with_cap(t, isWait, abstime);
        }

        return pop_impl_with_signal(t, isWait, abstime);
    }

    std::size_t capacity() const
    {
        return cap_;
    }

    bool closed() const
    {
        return closed_;
    }

    std::size_t size()
    {
        std::unique_lock<Mutex> lock(mtx_);
        return q_.size();
    }

    std::size_t empty()
    {
        return !size();
    }

    void close()
    {
        std::unique_lock<Mutex> lock(mtx_);
        closed_ = true;
        if (!cap_) {
            pushQ_ = nullptr;
            popQ_ = nullptr;
            if (pushWaiting_) {
                pushWaiting_->fast_notify_all(lock);
                pushWaiting_ = nullptr;
            }
            if (popWaiting_) {
                popWaiting_->fast_notify_all(lock);
                popWaiting_ = nullptr;
            }
        }

        pushCv_.fast_notify_all(lock);
        popCv_.fast_notify_all(lock);
        QueueT q;
        std::swap(q, q_);
    }

private:
    template<typename _Clock, typename _Duration>
    bool push_impl_with_cap(T const& t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        std::unique_lock<Mutex> lock(mtx_);

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | ptr(t)=0x%p | isWait=%d | abstime=%d | closed=%d | cap=%lu | size=%lu",
            id(), __func__, (void*)&t, isWait, !!abstime, closed_, cap_, q_.size());

        if (closed_)
            return false;

        if (q_.size() < cap_) {
            q_.push_back(t);
            popCv_.notify_one();

            RS_DBG(dbg_channel, "channel(queue)=%ld | %s | ptr(t)=0x%p | push success | notify",
                id(), __func__, (void*)&t);

            return true;
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel(queue)=%ld | %s | not match && not wait | return false",
                id(), __func__);
            return false;
        }

        auto p = [this]{ return q_.size() < cap_; };

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | begin wait",
                id(), __func__);

        std::cv_status status = pushCv_.wait_until_p(lock, abstime, p);

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | waked | status=%s | closed=%d",
                id(), __func__,
                (status == std::cv_status::timeout) ? "timeout" : "no_timeout", closed_);

        if (status == std::cv_status::timeout)
            return false;

        if (closed_)
            return false;

        q_.push_back(t);
        popCv_.notify_one();

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | ptr(t)=0x%p | push success after wait | size=%lu | notify",
                id(), __func__, (void*)&t, q_.size());

        return true;
    }

    template<typename _Clock, typename _Duration>
    bool pop_impl_with_cap(T & t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        std::unique_lock<Mutex> lock(mtx_);

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | ptr(t)=0x%p | isWait=%d | abstime=%d | closed=%d | cap=%lu | size=%lu",
            id(), __func__, (void*)&t, isWait, !!abstime, closed_, cap_, q_.size());

        if (closed_)
            return false;

        if (!q_.empty()) {
            t = q_.front();
            q_.pop_front();
            pushCv_.notify_one();

            RS_DBG(dbg_channel, "channel(queue)=%ld | %s | ptr(t)=0x%p | pop success | notify",
                id(), __func__, (void*)&t);

            return true;
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel(queue)=%ld | %s | not match && not wait | return false",
                id(), __func__);
            return false;
        }

        auto p = [this]{ return !q_.empty(); };

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | begin wait",
                id(), __func__);

        std::cv_status status = popCv_.wait_until_p(lock, abstime, p);

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | waked | status=%s | closed=%d",
                id(), __func__,
                (status == std::cv_status::timeout) ? "timeout" : "no_timeout", closed_);

        if (status == std::cv_status::timeout)
            return false;

        if (closed_)
            return false;

        t = q_.front();
        q_.pop_front();
        pushCv_.notify_one();

        RS_DBG(dbg_channel, "channel(queue)=%ld | %s | ptr(t)=0x%p | pop success after wait | size=%lu | notify",
                id(), __func__, (void*)&t, q_.size());

        return true;
    }

private:
    QueueT q_;
    std::size_t cap_;
};

// 仅计数
template <
    typename QueueT
>
class ChannelImpl<nullptr_t, QueueT> : public ChannelImplWithSignal<nullptr_t>
{
public:
    using ChannelImplWithSignal<nullptr_t>::mtx_;
    using ChannelImplWithSignal<nullptr_t>::pushCv_;
    using ChannelImplWithSignal<nullptr_t>::popCv_;
    using ChannelImplWithSignal<nullptr_t>::closed_;
    using ChannelImplWithSignal<nullptr_t>::pushQ_;
    using ChannelImplWithSignal<nullptr_t>::popQ_;
    using ChannelImplWithSignal<nullptr_t>::popWaiting_;
    using ChannelImplWithSignal<nullptr_t>::pushWaiting_;
    using ChannelImplWithSignal<nullptr_t>::pop_impl_with_signal;
    using ChannelImplWithSignal<nullptr_t>::push_impl_with_signal;
    using ChannelImplWithSignal<nullptr_t>::id;

    explicit ChannelImpl(std::size_t capacity = 0)
        : cap_(capacity), count_(0)
    {
    }

    template<typename _Clock, typename _Duration>
    bool push(nullptr_t const& t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        if (cap_) {
            return push_impl_with_cap(t, isWait, abstime);
        }

        return push_impl_with_signal(t, isWait, abstime);
    }

    template<typename _Clock, typename _Duration>
    bool pop(nullptr_t & t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        if (cap_) {
            return pop_impl_with_cap(t, isWait, abstime);
        }

        return pop_impl_with_signal(t, isWait, abstime);
    }

    std::size_t capacity() const
    {
        return cap_;
    }

    bool closed() const
    {
        return closed_;
    }

    std::size_t size()
    {
        std::unique_lock<Mutex> lock(mtx_);
        return count_;
    }

    std::size_t empty()
    {
        return !size();
    }

    void close()
    {
        std::unique_lock<Mutex> lock(mtx_);
        closed_ = true;
        if (!cap_) {
            pushQ_ = nullptr;
            popQ_ = nullptr;
            if (pushWaiting_) {
                pushWaiting_->fast_notify_all(lock);
                pushWaiting_ = nullptr;
            }
            if (popWaiting_) {
                popWaiting_->fast_notify_all(lock);
                popWaiting_ = nullptr;
            }
        }

        pushCv_.fast_notify_all(lock);
        popCv_.fast_notify_all(lock);
        count_ = 0;
    }

private:
    template<typename _Clock, typename _Duration>
    bool push_impl_with_cap(nullptr_t const& t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        std::unique_lock<Mutex> lock(mtx_);

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | isWait=%d | abstime=%d | closed=%d | cap=%lu | size=%lu",
            id(), __func__, isWait, !!abstime, closed_, cap_, count_);

        if (closed_)
            return false;

        if (count_ < cap_) {
            ++count_;
            popCv_.notify_one();

            RS_DBG(dbg_channel, "channel(void)=%ld | %s | push success | notify",
                id(), __func__);

            return true;
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel(void)=%ld | %s | not match && not wait | return false",
                id(), __func__);
            return false;
        }

        auto p = [this]{ return count_ < cap_; };

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | begin wait",
                id(), __func__);

        std::cv_status status = pushCv_.wait_until_p(lock, abstime, p);

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | waked | status=%s | closed=%d",
                id(), __func__,
                (status == std::cv_status::timeout) ? "timeout" : "no_timeout", closed_);

        if (status == std::cv_status::timeout)
            return false;

        if (closed_)
            return false;

        ++count_;
        popCv_.notify_one();

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | push success after wait | size=%lu | notify",
                id(), __func__, count_);

        return true;
    }

    template<typename _Clock, typename _Duration>
    bool pop_impl_with_cap(nullptr_t & t, bool isWait,
            const std::chrono::time_point<_Clock, _Duration>* abstime)
    {
        std::unique_lock<Mutex> lock(mtx_);

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | isWait=%d | abstime=%d | closed=%d | cap=%lu | size=%lu",
            id(), __func__, isWait, !!abstime, closed_, cap_, count_);

        if (closed_)
            return false;

        if (count_ > 0) {
            --count_;
            pushCv_.notify_one();

            RS_DBG(dbg_channel, "channel(void)=%ld | %s | pop success | notify",
                id(), __func__);
            return true;
        }

        if (!isWait) {
            RS_DBG(dbg_channel, "channel(void)=%ld | %s | not match && not wait | return false",
                id(), __func__);
            return false;
        }

        auto p = [this]{ return count_ > 0; };

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | begin wait",
                id(), __func__);

        std::cv_status status = popCv_.wait_until_p(lock, abstime, p);

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | waked | status=%s | closed=%d",
                id(), __func__,
                (status == std::cv_status::timeout) ? "timeout" : "no_timeout", closed_);

        if (status == std::cv_status::timeout)
            return false;

        if (closed_)
            return false;

        --count_;
        pushCv_.notify_one();

        RS_DBG(dbg_channel, "channel(void)=%ld | %s | pop success after wait | size=%lu | notify",
                id(), __func__, count_);

        return true;
    }

private:
    std::size_t cap_;
    std::size_t count_;
};

template <
    typename T,
    typename QueueT
>
class Channel
{
    typedef ChannelImpl<T, QueueT> ImplType;

public:
    explicit Channel(std::size_t capacity = 0,
            bool throw_ex_if_operator_failed = true)
    {
        impl_ = std::make_shared<ImplType>(capacity);
        throw_ex_if_operator_failed_ = throw_ex_if_operator_failed;
    }

    Channel const& operator<<(T const& t) const
    {
        if (!impl_->push(t, true, RoutineSyncTimer::null_tp()) && throw_ex_if_operator_failed_) {
            throw std::runtime_error("channel operator<<(T) error");
        }
        return *this;
    }

    template <typename U>
    typename std::enable_if<
        !std::is_same<U, std::nullptr_t>::value && std::is_same<U, T>::value,
        Channel const&>::type
    operator>>(U & t) const
    {
        if (!impl_->pop(t, true, RoutineSyncTimer::null_tp()) && throw_ex_if_operator_failed_) {
            throw std::runtime_error("channel operator>>(T) error");
        }
        return *this;
    }

    Channel const& operator>>(std::nullptr_t ignore) const
    {
        T t;
        if (!impl_->pop(t, true, RoutineSyncTimer::null_tp()) && throw_ex_if_operator_failed_) {
            throw std::runtime_error("channel operator<<(ignore) error");
        }
        return *this;
    }

    bool push(T const& t) const
    {
        return impl_->push(t, true, RoutineSyncTimer::null_tp());
    }

    template <typename U>
    typename std::enable_if<
        !std::is_same<U, std::nullptr_t>::value && std::is_same<U, T>::value,
        bool>::type
    pop(U & t) const
    {
        return impl_->pop(t, true, RoutineSyncTimer::null_tp());
    }

    bool pop(std::nullptr_t ignore) const
    {
        T t;
        return impl_->pop(t, true, RoutineSyncTimer::null_tp());
    }

    bool try_push(T const& t) const
    {
        return impl_->push(t, false, RoutineSyncTimer::null_tp());
    }

    template <typename U>
    typename std::enable_if<
        !std::is_same<U, std::nullptr_t>::value && std::is_same<U, T>::value,
        bool>::type
    try_pop(U & t) const
    {
        return impl_->pop(t, false, RoutineSyncTimer::null_tp());
    }

    bool try_pop(std::nullptr_t ignore) const
    {
        T t;
        return impl_->pop(t, false, RoutineSyncTimer::null_tp());
    }

    template <typename Rep, typename Period>
    bool push_for(T const& t, std::chrono::duration<Rep, Period> dur) const
    {
        auto abstime = RoutineSyncTimer::now() + dur;
        return impl_->push(t, true, &abstime);
    }

    bool push_until(T const& t, RoutineSyncTimer::clock_type::time_point deadline) const
    {
        return impl_->push(t, true, &deadline);
    }

    template <typename U, typename Rep, typename Period>
    typename std::enable_if<
        !std::is_same<U, std::nullptr_t>::value && std::is_same<U, T>::value,
        bool>::type
    pop_for(U & t, std::chrono::duration<Rep, Period> dur) const
    {
        auto abstime = RoutineSyncTimer::now() + dur;
        return impl_->pop(t, true, &abstime);
    }

    template <typename Rep, typename Period>
    bool pop_for(std::nullptr_t ignore, std::chrono::duration<Rep, Period> dur) const
    {
        auto abstime = RoutineSyncTimer::now() + dur;
        T t;
        return impl_->pop(t, true, &abstime);
    }

    template <typename U>
    typename std::enable_if<
        !std::is_same<U, std::nullptr_t>::value && std::is_same<U, T>::value,
        bool>::type
    pop_until(U & t, RoutineSyncTimer::clock_type::time_point deadline) const
    {
        return impl_->pop(t, true, &deadline);
    }

    // todo: template abstime
    bool pop_until(std::nullptr_t ignore, RoutineSyncTimer::clock_type::time_point deadline) const
    {
        T t;
        return impl_->pop(t, true, &deadline);
    }

    bool unique() const
    {
        return impl_.unique();
    }

    void close() const {
        impl_->close();
    }

    inline bool closed() const {
        return impl_->closed();
    }

    bool empty() const
    {
        return impl_->empty();
    }

    std::size_t size() const
    {
        return impl_->size();
    }

private:
    bool throw_ex_if_operator_failed_;
    std::shared_ptr<ImplType> impl_;
};

template <
    typename QueueT
>
class Channel<void, QueueT> : public Channel<std::nullptr_t, QueueT>
{
public:
    explicit Channel(std::size_t capacity = 0)
        : Channel<std::nullptr_t>(capacity)
    {}
};

} //namespace libgo
