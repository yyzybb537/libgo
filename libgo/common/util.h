#pragma once
#include "config.h"

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

// 侵入式引用计数智能指针
template <typename T>
class SharedPtr
{
public:
    SharedPtr() : ptr_(nullptr) {}
    explicit SharedPtr(T* ptr) : ptr_(ptr) {
        if (ptr_) ptr_->IncrementRef();
    }
    ~SharedPtr() {
        if (ptr_) ptr_->DecrementRef();
    }

    SharedPtr(SharedPtr const& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->IncrementRef();
    }
    SharedPtr(SharedPtr && other) {
        std::swap(ptr_, other.ptr_);
    }
    SharedPtr& operator=(SharedPtr const& other) {
        if (this == &other) return *this;
        reset();
        ptr_ = other.ptr_;
        if (ptr_) ptr_->IncrementRef();
        return *this;
    }
    SharedPtr& operator=(SharedPtr && other) {
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

    friend inline bool operator==(SharedPtr const& lhs, SharedPtr const& rhs) {
        return lhs.ptr_ == rhs.ptr_;
    }
    friend inline bool operator!=(SharedPtr const& lhs, SharedPtr const& rhs) {
        return lhs.ptr_ != rhs.ptr_;
    }
    friend inline bool operator<(SharedPtr const& lhs, SharedPtr const& rhs) {
        return lhs.ptr_ < rhs.ptr_;
    }

private:
    T* ptr_;
};

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
    atomic_t<long> reference_;
    Deleter deleter_;

    RefObject() : reference_{1} {}
    virtual ~RefObject() {}

    void IncrementRef()
    {
        ++reference_;
    }

    void DecrementRef()
    {
        if (--reference_ == 0)
            deleter_(this);
    }

    void SetDeleter(Deleter d) {
        deleter_ = d;
    }

    RefObject(RefObject const&) = delete;
    RefObject& operator=(RefObject const&) = delete;
};

inline void Deleter::operator()(RefObject* ptr) {
    if (func_)
        func_(ptr, arg_);
    else
        delete ptr;
}

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
        if (lhs.file_ != rhs.file_)
            return lhs.file_ < rhs.file_;

        return lhs.lineno_ < rhs.lineno_;
    }

    std::string to_string() const
    {
        std::string s("{file:");
        if (file_) s += file_;
        s += ", line:";
        s += std::to_string(lineno_) + "}";
        return s;
    }
};


} //namespace co
