#pragma once
#include <libgo/config.h>
#include <libgo/util.h>
#include <libgo/spinlock.h>

namespace co
{

// 侵入式数据结构Hook基类
struct TSQueueHook
{
    TSQueueHook *prev = nullptr;
    TSQueueHook *next = nullptr;
    void *check_ = nullptr;
};

// 侵入式双向链表
// moveable, noncopyable, foreachable
// erase, size, empty: O(1)
template <typename T>
class SList
{
    static_assert((std::is_base_of<TSQueueHook, T>::value), "T must be baseof TSQueueHook");

public:
    struct iterator
    {
        T* ptr;

        iterator() : ptr(nullptr) {}
        explicit iterator(T* p) : ptr(p) {}
        friend bool operator==(iterator const& lhs, iterator const& rhs)
        { return lhs.ptr == rhs.ptr; }
        friend bool operator!=(iterator const& lhs, iterator const& rhs)
        { return !(lhs.ptr == rhs.ptr); }
        iterator& operator++() { ptr = (T*)ptr->next; return *this; }
        iterator operator++(int) { iterator ret = *this; ++(*this); return ret; }
        iterator& operator--() { ptr = (T*)ptr->prev; return *this; }
        iterator operator--(int) { iterator ret = *this; --(*this); return ret; }
        T& operator*() { return *(T*)ptr; }
        T* operator->() { return (T*)ptr; }
    };

    T* head_;
    T* tail_;
    void *check_;
    std::size_t count_;

public:
    SList() : head_(nullptr), tail_(nullptr), check_(nullptr), count_(0) {}

    SList(TSQueueHook* h, TSQueueHook* t, std::size_t count, void *c)
        : head_((T*)h), tail_((T*)t), check_(c), count_(count) {}

    SList(SList const&) = delete;
    SList& operator=(SList const&) = delete;

    SList(SList<T> && other)
    {
        head_ = other.head_;
        tail_ = other.tail_;
        check_ = other.check_;
        count_ = other.count_;
        other.stealed();
    }

    SList& operator=(SList<T> && other)
    {
        clear();
        head_ = other.head_;
        tail_ = other.tail_;
        check_ = other.check_;
        count_ = other.count_;
        other.stealed();
        return *this;
    }

    ~SList()
    {
        clear();
    }

    iterator begin() { return iterator{head_}; }
    iterator end() { return iterator(); }
    ALWAYS_INLINE bool empty() const { return head_ == nullptr; }
    iterator erase(iterator it)
    {
        T* ptr = (it++).ptr;
        if (ptr->prev) ptr->prev->next = ptr->next;
        else head_ = (T*)head_->next;
        if (ptr->next) ptr->next->prev = ptr->prev;
        else tail_ = (T*)tail_->prev;
        ptr->prev = ptr->next = nullptr;
        ptr->check_ = nullptr;
        -- count_;
        DecrementRef(ptr);
        return it;
    }
    std::size_t size() const
    {
        return count_;
    }
    void clear()
    {
        auto it = begin();
        while (it != end())
            it = erase(it);
        stealed();
    }
    void stealed()
    {
        head_ = tail_ = nullptr;
        check_ = nullptr;
        count_ = 0;
    }

    ALWAYS_INLINE TSQueueHook* head() { return head_; }
    ALWAYS_INLINE TSQueueHook* tail() { return tail_; }
    ALWAYS_INLINE bool check(void *c) { return check_ == c; }
};

// 线程安全的队列(支持随机删除)
template <typename T, bool ThreadSafe = true>
class TSQueue
{
    static_assert((std::is_base_of<TSQueueHook, T>::value), "T must be baseof TSQueueHook");

//private:
public:
    LFLock lck;
    typedef typename std::conditional<ThreadSafe,
            std::lock_guard<LFLock>,
            fake_lock_guard>::type LockGuard;
    TSQueueHook* head_;
    TSQueueHook* tail_;
    std::size_t count_;
    void *check_ = nullptr;

public:
    TSQueue()
    {
        head_ = tail_ = new TSQueueHook;
        count_ = 0;
        check_ = this;
    }

    ~TSQueue()
    {
        LockGuard lock(lck);
        while (head_ != tail_) {
            TSQueueHook *prev = tail_->prev;
            DecrementRef((T*)tail_);
            tail_ = prev;
        }
        delete head_;
        head_ = tail_ = 0;
    }

    ALWAYS_INLINE bool empty()
    {
        LockGuard lock(lck);
        return head_ == tail_;
    }

    ALWAYS_INLINE std::size_t size()
    {
        LockGuard lock(lck);
        return count_;
    }

    ALWAYS_INLINE void push(T* element)
    {
        LockGuard lock(lck);
        TSQueueHook *hook = static_cast<TSQueueHook*>(element);
        tail_->next = hook;
        hook->prev = tail_;
        hook->next = nullptr;
        hook->check_ = check_;
        tail_ = hook;
        ++ count_;
        IncrementRef(element);
    }

    ALWAYS_INLINE T* pop()
    {
        if (head_ == tail_) return nullptr;
        LockGuard lock(lck);
        if (head_ == tail_) return nullptr;
        TSQueueHook* ptr = head_->next;
        if (ptr == tail_) tail_ = head_;
        head_->next = ptr->next;
        if (ptr->next) ptr->next->prev = head_;
        ptr->prev = ptr->next = nullptr;
        ptr->check_ = nullptr;
        -- count_;
        DecrementRef((T*)ptr);
        return (T*)ptr;
    }

    ALWAYS_INLINE void push(SList<T> && elements)
    {
        if (elements.empty()) return ;  // empty的SList不能check, 因为stealed的时候已经清除check_.
        assert(elements.check(check_));
        LockGuard lock(lck);
        count_ += elements.size();
        tail_->next = elements.head();
        elements.head()->prev = tail_;
        elements.tail()->next = nullptr;
        tail_ = elements.tail();
        elements.stealed();
    }

    // O(n), 慎用.
    ALWAYS_INLINE SList<T> pop_front(uint32_t n)
    {
        if (head_ == tail_) return SList<T>();
        LockGuard lock(lck);
        if (head_ == tail_) return SList<T>();
        TSQueueHook* first = head_->next;
        TSQueueHook* last = first;
        uint32_t c = 1;
        for (; c < n && last->next; ++c)
            last = last->next;
        if (last == tail_) tail_ = head_;
        head_->next = last->next;
        if (last->next) last->next->prev = head_;
        first->prev = last->next = nullptr;
        count_ -= c;
        return SList<T>(first, last, c, check_);
    }

    // O(n), 慎用.
    ALWAYS_INLINE SList<T> pop_back(uint32_t n)
    {
        if (head_ == tail_) return SList<T>();
        LockGuard lock(lck);
        if (head_ == tail_) return SList<T>();
        TSQueueHook* last = tail_;
        TSQueueHook* first = last;
        uint32_t c = 1;
        for (; c < n && first->prev && first->prev != head_; ++c)
            first = first->prev;
        tail_ = first->prev;
        first->prev = tail_->next = nullptr;
        count_ -= c;
        return SList<T>(first, last, c, check_);
    }

    ALWAYS_INLINE SList<T> pop_all()
    {
        if (head_ == tail_) return SList<T>();
        LockGuard lock(lck);
        if (head_ == tail_) return SList<T>();
        TSQueueHook* first = head_->next;
        TSQueueHook* last = tail_;
        tail_ = head_;
        head_->next = nullptr;
        first->prev = last->next = nullptr;
        std::size_t c = count_;
        count_ = 0;
        return SList<T>(first, last, c, check_);
    }

    ALWAYS_INLINE bool erase(T* hook)
    {
        LockGuard lock(lck);
        if (hook->check_ != (void*)check_) return false;
        if (hook->prev) hook->prev->next = hook->next;
        if (hook->next) hook->next->prev = hook->prev;
        else if (hook == tail_) tail_ = tail_->prev;
        hook->prev = hook->next = nullptr;
        hook->check_ = nullptr;
        -- count_;
        DecrementRef((T*)hook);
        return true;
    }
};

// 线程安全的跳表队列(支持快速pop出n个元素)
template <typename T,
         bool ThreadSafe = true,
         std::size_t SkipDistance = 64  // 必须是2的n次方
         >
class TSSkipQueue
{
    static_assert((std::is_base_of<TSQueueHook, T>::value), "T must be baseof TSQueueHook");

private:
    LFLock lck;
    typedef typename std::conditional<ThreadSafe,
            std::lock_guard<LFLock>,
            fake_lock_guard>::type LockGuard;
    TSQueueHook* head_;
    TSQueueHook* tail_;
    std::size_t count_;

    struct SkipLayer
    {
        std::deque<TSQueueHook*> indexs_;
        std::size_t head_offset_ = 0;  // begin距离head的距离
        std::size_t tail_offset_ = 0;  // end距离tail的距离
    };

    SkipLayer skip_layer_;

public:
    TSSkipQueue()
    {
        head_ = tail_ = new TSQueueHook;
        count_ = 0;
    }

    ~TSSkipQueue()
    {
        LockGuard lock(lck);
        while (head_ != tail_) {
            TSQueueHook *prev = tail_->prev;
            DecrementRef((T*)tail_);
            tail_ = prev;
        }
        delete head_;
        head_ = tail_ = 0;
    }

    bool empty()
    {
        LockGuard lock(lck);
        return head_ == tail_;
    }

    std::size_t size()
    {
        LockGuard lock(lck);
        return count_;
    }

    void push(T* element)
    {
        LockGuard lock(lck);
        // 插入到尾端
        TSQueueHook *hook = static_cast<TSQueueHook*>(element);
        tail_->next = hook;
        hook->prev = tail_;
        hook->next = nullptr;
        hook->check_ = this;
        tail_ = hook;
        ++ count_;
        IncrementRef(element);

        // 刷新跳表
        if (++skip_layer_.tail_offset_ == SkipDistance) {
            skip_layer_.tail_offset_ = 0;
            skip_layer_.indexs_.push_back(tail_);
        }
    }

    SList<T> pop_front(std::size_t n)
    {
        if (head_ == tail_ || !n) return SList<T>();
        LockGuard lock(lck);
        if (head_ == tail_) return SList<T>();
        TSQueueHook* first = head_->next;
        TSQueueHook* last = first;
        n = (std::min)(n, count_);
        std::size_t next_step = n - 1;

        // 从跳表上查找
        if ((SkipDistance - skip_layer_.head_offset_) > n) {
            skip_layer_.head_offset_ += n;
        } else if (skip_layer_.indexs_.empty()) {
            skip_layer_.tail_offset_ = count_ - n;
        } else {
            // 往前找
            std::size_t forward = (n + skip_layer_.head_offset_ - SkipDistance) / SkipDistance;
            next_step = (n + skip_layer_.head_offset_ - SkipDistance) & (SkipDistance - 1);
            auto it = skip_layer_.indexs_.begin();
            std::advance(it, forward);
            last = *it;
            skip_layer_.indexs_.erase(skip_layer_.indexs_.begin(), ++it);
            if (skip_layer_.indexs_.empty()) {
                skip_layer_.tail_offset_ = count_ - n;
                skip_layer_.head_offset_ = 0;
            } else {
                skip_layer_.head_offset_ = next_step;
            }
        }

        for (std::size_t i = 0; i < next_step; ++i)
            last = last->next;

        if (last == tail_) tail_ = head_;
        head_->next = last->next;
        if (last->next) last->next->prev = head_;
        first->prev = last->next = nullptr;
        count_ -= n;
        return SList<T>(first, last, n, this);
    }
};

} //namespace co
