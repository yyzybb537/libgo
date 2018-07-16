#pragma once
#include "config.h"
#include "util.h"
#include "spinlock.h"

namespace co
{

// 侵入式数据结构Hook基类
struct TSQueueHook
{
    TSQueueHook* prev = nullptr;
    TSQueueHook* next = nullptr;
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
    // !! 支持边遍历边删除 !!
    struct iterator
    {
        T* ptr;
        T* prev;
        T* next;

        iterator() : ptr(nullptr), prev(nullptr), next(nullptr) {}
        iterator(T* p) { reset(p); }
        void reset(T* p) {
            ptr = p;
            next = ptr ? (T*)ptr->next : nullptr;
            prev = ptr ? (T*)ptr->prev : nullptr;
        }

        friend bool operator==(iterator const& lhs, iterator const& rhs)
        { return lhs.ptr == rhs.ptr; }
        friend bool operator!=(iterator const& lhs, iterator const& rhs)
        { return !(lhs.ptr == rhs.ptr); }

        iterator& operator++() { reset(next); return *this; }
        iterator operator++(int) { iterator ret = *this; ++(*this); return ret; }
        iterator& operator--() { reset(prev); return *this; }
        iterator operator--(int) { iterator ret = *this; --(*this); return ret; }
        T& operator*() { return *(T*)ptr; }
        T* operator->() { return (T*)ptr; }
    };

    T* head_;
    T* tail_;
    std::size_t count_;

public:
    SList() : head_(nullptr), tail_(nullptr), count_(0) {}

    SList(TSQueueHook* h, TSQueueHook* t, std::size_t count)
        : head_((T*)h), tail_((T*)t), count_(count) {}

    SList(SList const&) = delete;
    SList& operator=(SList const&) = delete;

    SList(SList<T> && other)
    {
        head_ = other.head_;
        tail_ = other.tail_;
        count_ = other.count_;
        other.stealed();
    }

    SList& operator=(SList<T> && other)
    {
        clear();
        head_ = other.head_;
        tail_ = other.tail_;
        count_ = other.count_;
        other.stealed();
        return *this;
    }

    void append(SList<T> && other) {
        if (other.empty())
            return ;

        if (empty()) {
            *this = std::move(other);
            return ;
        }

        tail_->next = other.head_;
        tail_ = other.tail_;
        count_ += other.count_;
        other.stealed();
    }

    SList<T> cut(std::size_t n) {
        if (empty()) return SList<T>();

        if (n >= size()) {
            SList<T> o(std::move(*this));
            return o;
        }

        if (n == 0) {
            return SList<T>();
        }

        SList<T> o;
        auto pos = head_;
        for (std::size_t i = 1; i < n; ++i)
            pos = (T*)pos->next;
        o.head_ = head_;
        o.tail_ = pos;
        o.count_ = n;

        count_ -= n;
        head_ = (T*)pos->next;
        head_->prev = nullptr;
        pos->next = nullptr;
        return o;
    }

    ~SList()
    {
        assert(count_ == 0);
    }

    iterator begin() { return iterator{head_}; }
    iterator end() { return iterator(); }
    ALWAYS_INLINE bool empty() const { return head_ == nullptr; }
    iterator erase(iterator it)
    {
        T* ptr = (it++).ptr;
        erase(ptr);
        return it;
    }
    bool erase(T* ptr, void *check)
    {
        if (ptr->check_ != check) return false;
        erase(ptr);
        return true;
    }
    void erase(T* ptr)
    {
        if (ptr->prev) ptr->prev->next = ptr->next;
        else head_ = (T*)head_->next;
        if (ptr->next) ptr->next->prev = ptr->prev;
        else tail_ = (T*)tail_->prev;
        ptr->prev = ptr->next = nullptr;
        -- count_;
//        printf("SList.erase ptr=%p, use_count=%ld\n", ptr, ptr->use_count());
        DecrementRef(ptr);
//        printf("SList.erase done\n");
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
        count_ = 0;
    }

    ALWAYS_INLINE TSQueueHook* head() { return head_; }
    ALWAYS_INLINE TSQueueHook* tail() { return tail_; }
};

// 线程安全的队列(支持随机删除)
template <typename T, bool ThreadSafe = true>
class TSQueue
{
    static_assert((std::is_base_of<TSQueueHook, T>::value), "T must be baseof TSQueueHook");

//private:
public:
    typedef typename std::conditional<ThreadSafe,
            LFLock, FakeLock>::type lock_t;
    typedef typename std::conditional<ThreadSafe,
            std::lock_guard<LFLock>,
            fake_lock_guard>::type LockGuard;
    lock_t lock_;
    TSQueueHook* head_;
    TSQueueHook* tail_;
    volatile std::size_t count_;
    void *check_; // 可选的erase检测

public:
    TSQueue()
    {
        head_ = tail_ = new TSQueueHook;
        count_ = 0;
        check_ = this;
    }

    ~TSQueue()
    {
        LockGuard lock(lock_);
        while (head_ != tail_) {
            TSQueueHook *prev = tail_->prev;
            DecrementRef((T*)tail_);
            tail_ = prev;
        }
        delete head_;
        head_ = tail_ = 0;
    }

    ALWAYS_INLINE lock_t& LockRef() {
        return lock_;
    }

    ALWAYS_INLINE void front(T*& out)
    {
        LockGuard lock(lock_);
        out = (T*)head_->next;
        if (out) out->check_ = check_;
    }

    ALWAYS_INLINE void next(T* ptr, T*& out)
    {
        LockGuard lock(lock_);
        out = (T*)ptr->next;
        if (out) out->check_ = check_;
    }

    ALWAYS_INLINE bool empty()
    {
        LockGuard lock(lock_);
        return !count_;
    }

    ALWAYS_INLINE bool emptyUnsafe()
    {
        return !count_;
    }

    ALWAYS_INLINE std::size_t size()
    {
        LockGuard lock(lock_);
        return count_;
    }

    ALWAYS_INLINE void push(T* element)
    {
        LockGuard lock(lock_);
        pushWithoutLock(element);
    }
    ALWAYS_INLINE void pushWithoutLock(T* element)
    {
        TSQueueHook *hook = static_cast<TSQueueHook*>(element);
        assert(hook->next == nullptr);
        assert(hook->prev == nullptr);
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
        LockGuard lock(lock_);
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
        if (elements.empty()) return ;
        assert(elements.head_->prev == nullptr);
        assert(elements.tail_->next == nullptr);
        LockGuard lock(lock_);
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
        LockGuard lock(lock_);
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
        return SList<T>(first, last, c);
    }

    // O(n), 慎用.
    ALWAYS_INLINE SList<T> pop_back(uint32_t n)
    {
        if (head_ == tail_) return SList<T>();
        LockGuard lock(lock_);
        return pop_backWithoutLock(n);
    }
    ALWAYS_INLINE SList<T> pop_backWithoutLock(uint32_t n)
    {
        if (head_ == tail_) return SList<T>();
        TSQueueHook* last = tail_;
        TSQueueHook* first = last;
        uint32_t c = 1;
        for (; c < n && first->prev && first->prev != head_; ++c)
            first = first->prev;
        tail_ = first->prev;
        first->prev = tail_->next = nullptr;
        count_ -= c;
        return SList<T>(first, last, c);
    }

    ALWAYS_INLINE SList<T> pop_all()
    {
        if (head_ == tail_) return SList<T>();
        LockGuard lock(lock_);
        return pop_allWithoutLock();
    }

    ALWAYS_INLINE SList<T> pop_allWithoutLock()
    {
        if (head_ == tail_) return SList<T>();
        TSQueueHook* first = head_->next;
        TSQueueHook* last = tail_;
        tail_ = head_;
        head_->next = nullptr;
        first->prev = last->next = nullptr;
        std::size_t c = count_;
        count_ = 0;
        return SList<T>(first, last, c);
    }

    ALWAYS_INLINE bool erase(T* hook, bool check = false)
    {
        LockGuard lock(lock_);
        return eraseWithoutLock(hook, check);
    }

    ALWAYS_INLINE bool eraseWithoutLock(T* hook, bool check = false)
    {
        if (check && hook->check_ != (void*)check_) return false;
        assert(hook->prev != nullptr);
        assert(hook == tail_ || hook->next != nullptr);
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

} //namespace co
