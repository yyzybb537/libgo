#pragma once
#include "../common/config.h"
#include "../common/spinlock.h"
#include <mutex>

namespace co
{

struct WaitQueueHook
{
    WaitQueueHook* next = nullptr;
};

template <typename T>
class WaitQueue
{
    static_assert(std::is_base_of<WaitQueueHook, T>::value, "");

public:
    typedef std::function<bool(T*)> Functor;
    typedef std::mutex lock_t;
    lock_t lock_;
    WaitQueueHook dummy_;
    WaitQueueHook* head_;
    WaitQueueHook* tail_;
    WaitQueueHook* check_;
    Functor checkFunctor_;
    WaitQueueHook* pos_;
    const size_t posDistance_;
    volatile size_t count_;
    Functor posFunctor_;

    struct CondRet
    {
        bool canQueue;
        bool needWait;
    };

    explicit WaitQueue(Functor checkFunctor = NULL,
            size_t posD = -1, Functor const& posFunctor = NULL)
        : posDistance_(posD)
    {
        head_ = &dummy_;
        tail_ = &dummy_;
        check_ = &dummy_;
        checkFunctor_ = checkFunctor;
        pos_ = nullptr;
        posFunctor_ = posFunctor;
        count_ = 0;
    }

    ~WaitQueue() {
        check_ = pos_ = nullptr;
        count_ = 0;
        T* ptr = nullptr;
        while (pop(ptr))
            delete ptr;
    }

    size_t empty()
    {
        return count_ == 0;
    }

    size_t size()
    {
        return count_;
    }

    CondRet push(T* ptr, std::function<CondRet(size_t)> const& cond = NULL)
    {
        std::unique_lock<lock_t> lock(lock_);
        CondRet ret{true, true};
        if (cond) {
            ret = cond(count_);
            if (!ret.canQueue)
                return ret;
        }

        tail_->next = ptr;
        ptr->next = nullptr;
        tail_ = ptr;
        if (++count_ == posDistance_) {
            pos_ = ptr;
        }

        // check
        if (count_ < (std::max)(posDistance_, posDistance_ + 16)) return ret;
        if (!checkFunctor_) return ret;

        if (!check_ || !check_->next)
            check_ = pos_;

        for (int i = 0; check_ && check_->next && i < 2; ++i) {
            if (!checkFunctor_(static_cast<T*>(check_->next))) {
                if (pos_ == check_->next)
                    pos_ = pos_->next;

                if (tail_ == check_->next)
                    tail_ = check_;

                auto temp = check_->next;
                check_->next = check_->next->next;
                temp->next = nullptr;
                static_cast<T*>(temp)->DecrementRef();
                continue;
            }

            check_ = check_->next;
        }
        return ret;
    }

    bool pop(T* & ptr)
    {
        std::unique_lock<lock_t> lock(lock_);
        if (head_ == tail_) return false;

        ptr = static_cast<T*>(head_->next);
        if (tail_ == head_->next) tail_ = head_;
        if (check_ == head_->next) check_ = check_->next;
        head_->next = head_->next->next;
        ptr->next = nullptr;

        for (;;) {
            if (!pos_ || !posFunctor_) break;

            bool ok = posFunctor_(static_cast<T*>(pos_));
            pos_ = pos_->next;
            if (ok) break;
        }

        --count_;
        return true;
    }

    bool tryPop(T* & ptr)
    {
        if (!count_) return false;
        return pop(ptr);
    }
};

} // namespace co
