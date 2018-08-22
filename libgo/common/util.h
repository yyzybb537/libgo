#pragma once
#include "config.h"
#include <string.h>

namespace co
{

// 可被优化的lock guard
struct fake_lock_guard
{
    template <typename Mutex>
    explicit fake_lock_guard(Mutex&) {}
};

///////////////////////////////////////
/*
* 这里构建了一个半侵入式的引用计数体系, 使用shared_ptr语意的同时,
* 又可以将对象放入侵入式容器, 得到极佳的性能.
*/

// 侵入式引用计数对象基类
struct RefObject;
struct RefObjectImpl;
struct SharedRefObject;

// 自定义delete
struct Deleter
{
    typedef void (*func_t)(RefObject* ptr, void* arg);
    func_t func_;
    void* arg_;

    Deleter() : func_(nullptr), arg_(nullptr) {}
    Deleter(func_t func, void* arg) : func_(func), arg_(arg) {}

    inline void operator()(RefObject* ptr);
};

struct RefObject
{
    atomic_t<long>* reference_;
    atomic_t<long> referenceImpl_;
    Deleter deleter_;

    RefObject() : referenceImpl_{1} {
        reference_ = &referenceImpl_;
    }
    virtual ~RefObject() {}

    bool IsShared() const {
        return reference_ != &referenceImpl_;
    }

    void IncrementRef()
    {
        ++*reference_;
    }

    virtual bool DecrementRef()
    {
        if (--*reference_ == 0) {
            deleter_(this);
            return true;
        }
        return false;
    }

    long use_count() {
        return *reference_;
    }

    void SetDeleter(Deleter d) {
        deleter_ = d;
    }

    RefObject(RefObject const&) = delete;
    RefObject& operator=(RefObject const&) = delete;
};

struct RefObjectImpl
{
    atomic_t<long> reference_;
    atomic_t<long> weak_;

    RefObjectImpl() : reference_{1}, weak_{1} {}

    void IncrementWeak()
    {
        ++weak_;
    }

    void DecrementWeak()
    {
        if (--weak_ == 0) {
//            printf("delete weak = %p, reference_ = %ld\n", this, (long)reference_);
            delete this;
        }
    }

    bool Lock()
    {
        long count = reference_.load(std::memory_order_relaxed);
        do {
            if (count == 0)
                return false;
        } while (!reference_.compare_exchange_weak(count, count + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed));

        return true;
    }

    RefObjectImpl(RefObjectImpl const&) = delete;
    RefObjectImpl& operator=(RefObjectImpl const&) = delete;
};

struct SharedRefObject : public RefObject
{
    RefObjectImpl * impl_;

    SharedRefObject() : impl_(new RefObjectImpl) {
        this->reference_ = &impl_->reference_;
    }

    virtual bool DecrementRef()
    {
        RefObjectImpl * impl = impl_;
        if (RefObject::DecrementRef()) {
            std::atomic_thread_fence(std::memory_order_acq_rel);
            impl->DecrementWeak();
            return true;
        }
        return false;
    }
};

inline void Deleter::operator()(RefObject* ptr) {
    if (func_)
        func_(ptr, arg_);
    else
        delete ptr;
}

// 侵入式引用计数智能指针
template <typename T>
class IncursivePtr
{
public:
    IncursivePtr() : ptr_(nullptr) {}
    explicit IncursivePtr(T* ptr) : ptr_(ptr) {
        if (ptr_) ptr_->IncrementRef();
    }
    ~IncursivePtr() {
        if (ptr_) ptr_->DecrementRef();
    }

    IncursivePtr(IncursivePtr const& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->IncrementRef();
    }
    IncursivePtr(IncursivePtr && other) {
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
    }
    IncursivePtr& operator=(IncursivePtr const& other) {
        if (this == &other) return *this;
        reset();
        ptr_ = other.ptr_;
        if (ptr_) ptr_->IncrementRef();
        return *this;
    }
    IncursivePtr& operator=(IncursivePtr && other) {
        if (this == &other) return *this;
        reset();
        std::swap(ptr_, other.ptr_);
        return *this;
    }

    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return !!ptr_; }
    T* get() const { return ptr_; }

    void reset() {
        if (ptr_) {
            ptr_->DecrementRef();
            ptr_ = nullptr;
        }
    }

    long use_count() {
        return ptr_ ? (long)*ptr_->reference_ : 0;
    }

    bool unique() {
        return use_count() == 1;
    }

    void swap(IncursivePtr & other) {
        std::swap(ptr_, other.ptr_);
    }

    friend inline bool operator==(IncursivePtr const& lhs, IncursivePtr const& rhs) {
        return lhs.ptr_ == rhs.ptr_;
    }
    friend inline bool operator!=(IncursivePtr const& lhs, IncursivePtr const& rhs) {
        return lhs.ptr_ != rhs.ptr_;
    }
    friend inline bool operator<(IncursivePtr const& lhs, IncursivePtr const& rhs) {
        return lhs.ptr_ < rhs.ptr_;
    }

private:
    T* ptr_;
};

// 弱指针
// 注意：弱指针和对象池不可一起使用, 弱指针无法区分已经归还到池的对象.
template <typename T>
class WeakPtr
{
public:
    WeakPtr() : impl_(nullptr), ptr_(nullptr) {}
    explicit WeakPtr(T * ptr) : impl_(nullptr), ptr_(nullptr) {
        reset(ptr);
    }
    explicit WeakPtr(IncursivePtr<T> const& iptr) : impl_(nullptr), ptr_(nullptr) {
        T* ptr = iptr.get();
        reset(ptr);
    }
    WeakPtr(WeakPtr const& other) : impl_(other.impl_), ptr_(other.ptr_) {
        if (impl_) impl_->IncrementWeak();
    }
    WeakPtr(WeakPtr && other) : impl_(nullptr), ptr_(nullptr) {
        swap(other);
    }
    WeakPtr& operator=(WeakPtr const& other) {
        if (this == &other) return *this;
        reset();
        if (other.impl_) {
            impl_ = other.impl_;
            ptr_ = other.ptr_;
            impl_->IncrementWeak();
        }
        return *this;
    }
    ~WeakPtr() {
        reset();
    }
    void swap(WeakPtr & other) {
        std::swap(impl_, other.impl_);
        std::swap(ptr_, other.ptr_);
    }
    void reset() {
        if (impl_) {
            impl_->DecrementWeak();
            impl_ = nullptr;
            ptr_ = nullptr;
        }
    }

    void reset(T * ptr) {
        if (impl_) {
            impl_->DecrementWeak();
            impl_ = nullptr;
            ptr_ = nullptr;
        }

        if (!ptr) return ;
        if (!ptr->IsShared()) return ;
        impl_ = ((SharedRefObject*)ptr)->impl_;
        ptr_ = ptr;
        impl_->IncrementWeak();
    }

    IncursivePtr<T> lock() const {
        if (!impl_) return IncursivePtr<T>();
        if (!impl_->Lock()) return IncursivePtr<T>();
        IncursivePtr<T> iptr(ptr_);
        ptr_->DecrementRef();
        return iptr;
    }

    explicit operator bool() const {
        return !!impl_;
    }

    friend bool operator==(WeakPtr<T> const& lhs, WeakPtr<T> const& rhs) {
        return lhs.impl_ == rhs.impl_ && lhs.ptr_ == rhs.ptr_;
    }

    long use_count() {
        return impl_ ? (long)impl_->reference_ : 0;
    }

private:
    RefObjectImpl * impl_;
    T* ptr_;
};

// 裸指针 -> shared_ptr
template <typename T>
typename std::enable_if<std::is_base_of<RefObject, T>::value, std::shared_ptr<T>>::type
SharedFromThis(T * ptr)
{
    ptr->IncrementRef();
    return std::shared_ptr<T>(ptr, [](T * self){ self->DecrementRef(); });
}

// make_shared
template <typename T, typename ... Args>
typename std::enable_if<std::is_base_of<RefObject, T>::value, std::shared_ptr<T>>::type
MakeShared(Args && ... args)
{
    T * ptr = new T(std::forward<Args>(args)...);
    return std::shared_ptr<T>(ptr, [](T * self){ self->DecrementRef(); });
}

template <typename T>
typename std::enable_if<std::is_base_of<RefObject, T>::value>::type
IncrementRef(T * ptr)
{
    ptr->IncrementRef();
}
template <typename T>
typename std::enable_if<!std::is_base_of<RefObject, T>::value>::type
IncrementRef(T * ptr)
{
}
template <typename T>
typename std::enable_if<std::is_base_of<RefObject, T>::value>::type
DecrementRef(T * ptr)
{
    ptr->DecrementRef();
}
template <typename T>
typename std::enable_if<!std::is_base_of<RefObject, T>::value>::type
DecrementRef(T * ptr)
{
}

// 引用计数guard
class RefGuard
{
public:
    template <typename T>
    explicit RefGuard(T* ptr) : ptr_(static_cast<RefObject*>(ptr))
    {
        ptr_->IncrementRef();
    }
    template <typename T>
    explicit RefGuard(T& obj) : ptr_(static_cast<RefObject*>(&obj))
    {
        ptr_->IncrementRef();
    }
    ~RefGuard()
    {
        ptr_->DecrementRef();
    }

    RefGuard(RefGuard const&) = delete;
    RefGuard& operator=(RefGuard const&) = delete;

private:
    RefObject *ptr_;
};

// 全局对象计数器
template <typename T>
struct ObjectCounter
{
    ObjectCounter() { ++counter(); }
    ObjectCounter(ObjectCounter const&) { ++counter(); }
    ObjectCounter(ObjectCounter &&) { ++counter(); }
    ~ObjectCounter() { --counter(); }

    static long getCount() {
        return counter();
    }

private:
    static atomic_t<long>& counter() {
        static atomic_t<long> c;
        return c;
    }
};

// ID
template <typename T>
struct IdCounter
{
    IdCounter() { id_ = ++counter(); }
    IdCounter(IdCounter const&) { id_ = ++counter(); }
    IdCounter(IdCounter &&) { id_ = ++counter(); }

    long getId() const {
        return id_;
    }

private:
    static atomic_t<long>& counter() {
        static atomic_t<long> c;
        return c;
    }

    long id_;
};

///////////////////////////////////////

// 创建协程的源码文件位置
struct SourceLocation
{
    const char* file_ = nullptr;
    int lineno_ = 0;

    void Init(const char* file, int lineno)
    {
        file_ = file, lineno_ = lineno;
    }

    friend bool operator<(SourceLocation const& lhs, SourceLocation const& rhs)
    {
        if (lhs.lineno_ != rhs.lineno_)
            return lhs.lineno_ < rhs.lineno_;

        if (lhs.file_ == rhs.file_) return false;

        if (lhs.file_ == nullptr)
            return true;

        if (rhs.file_ == nullptr)
            return false;
        
        return strcmp(lhs.file_, rhs.file_) == -1;
    }

    std::string ToString() const
    {
        std::string s("{file:");
        if (file_) s += file_;
        s += ", line:";
        s += std::to_string(lineno_) + "}";
        return s;
    }
};


} //namespace co
