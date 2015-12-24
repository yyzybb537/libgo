#pragma once
#include <mutex>
#include <assert.h>
#include "spinlock.h"

namespace co
{

struct fake_lock_guard
{
    template <typename Mutex>
    explicit fake_lock_guard(Mutex&) {}
};

struct TSQueueHook
{
    TSQueueHook *prev = NULL;
    TSQueueHook *next = NULL;
    void *check_ = NULL;
};

template <typename T>
class SList
{
public:
    struct iterator;
    struct iterator
    {
        TSQueueHook* ptr;

        iterator() : ptr(NULL) {}
        explicit iterator(TSQueueHook* p) : ptr(p) {}
        friend bool operator==(iterator const& lhs, iterator const& rhs)
        { return lhs.ptr == rhs.ptr; }
        friend bool operator!=(iterator const& lhs, iterator const& rhs)
        { return !(lhs.ptr == rhs.ptr); }
        iterator& operator++() { ptr = ptr->next; return *this; }
        iterator operator++(int) { iterator ret = *this; ++(*this); return ret; }
        iterator& operator--() { ptr = ptr->prev; return *this; }
        iterator operator--(int) { iterator ret = *this; --(*this); return ret; }
        T& operator*() { return *(T*)ptr; }
        T* operator->() { return (T*)ptr; }
    };

    TSQueueHook* head_;
    TSQueueHook* tail_;
    void *check_;
    std::size_t count_;

public:
    SList() : head_(NULL), tail_(NULL), check_(NULL), count_(0) {}
    SList(TSQueueHook* h, TSQueueHook* t, std::size_t count, void *c)
        : head_(h), tail_(t), check_(c), count_(count) {}

    iterator begin() { return iterator{head_}; }
    iterator end() { return iterator(); }
    inline bool empty() const { return head_ == NULL; }
    iterator erase(iterator it)
    {
        TSQueueHook* hook = (it++).ptr;
        if (hook->prev) hook->prev->next = hook->next;
        else head_ = head_->next;
        if (hook->next) hook->next->prev = hook->prev;
        else tail_ = tail_->prev;
        hook->prev = hook->next = NULL;
        hook->check_ = NULL;
        -- count_;
        return it;
    }
    std::size_t size() const
    {
        return count_;
    }

    inline TSQueueHook* head() { return head_; }
    inline TSQueueHook* tail() { return tail_; }
    inline bool check(void *c) { return check_ == c; }
};

template <typename T, bool ThreadSafe = true>
class TSQueue
{
    LFLock lck;
    typedef typename std::conditional<ThreadSafe,
            std::lock_guard<LFLock>,
            fake_lock_guard>::type LockGuard;
    TSQueueHook* head_;
    TSQueueHook* tail_;
    std::size_t count_;

public:
    TSQueue()
    {
        head_ = tail_ = new TSQueueHook;
        count_ = 0;
    }

    ~TSQueue()
    {
        LockGuard lock(lck);
        while (head_ != tail_) {
            TSQueueHook *prev = tail_->prev;
            delete (T*)tail_;
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
        TSQueueHook *hook = static_cast<TSQueueHook*>(element);
        tail_->next = hook;
        hook->prev = tail_;
        hook->next = NULL;
        hook->check_ = this;
        tail_ = hook;
        ++ count_;
    }

    T* pop()
    {
        if (head_ == tail_) return NULL;
        LockGuard lock(lck);
        if (head_ == tail_) return NULL;
        TSQueueHook* ptr = head_->next;
        if (ptr == tail_) tail_ = head_;
        head_->next = ptr->next;
        if (ptr->next) ptr->next->prev = head_;
        ptr->prev = ptr->next = NULL;
        ptr->check_ = NULL;
        -- count_;
        return (T*)ptr;
    }

    void push(SList<T> elements)
    {
        assert(elements.check(this));
        if (elements.empty()) return ;
        LockGuard lock(lck);
        count_ += elements.size();
        tail_->next = elements.head();
        elements.head()->prev = tail_;
        elements.tail()->next = NULL;
        tail_ = elements.tail();
    }

    // O(n), 慎用.
    SList<T> pop(uint32_t n)
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
        first->prev = last->next = NULL;
        count_ -= c;
        return SList<T>(first, last, c, this);
    }

    SList<T> pop_all()
    {
        if (head_ == tail_) return SList<T>();
        LockGuard lock(lck);
        if (head_ == tail_) return SList<T>();
        TSQueueHook* first = head_->next;
        TSQueueHook* last = tail_;
        tail_ = head_;
        head_->next = NULL;
        first->prev = last->next = NULL;
        std::size_t c = count_;
        count_ = 0;
        return SList<T>(first, last, c, this);
    }


    bool erase(TSQueueHook* hook)
    {
        LockGuard lock(lck);
        if (hook->check_ != (void*)this) return false;
        if (hook->prev) hook->prev->next = hook->next;
        if (hook->next) hook->next->prev = hook->prev;
        else if (hook == tail_) tail_ = tail_->prev;
        hook->prev = hook->next = NULL;
        hook->check_ = NULL;
        -- count_;
        return true;
    }
};


} //namespace co
