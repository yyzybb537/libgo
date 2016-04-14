#pragma once
#include <memory>
#include <atomic>
#include <type_traits>

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
struct RefObject
{
    std::atomic<long> reference_;

    RefObject() : reference_{1} {}
    virtual ~RefObject() {}

    void IncrementRef()
    {
        ++reference_;
    }

    void DecrementRef()
    {
        if (--reference_ == 0)
            delete this;
    }

    RefObject(RefObject const&) = delete;
    RefObject& operator=(RefObject const&) = delete;
};

// 裸指针 -> shared_ptr
template <typename T>
typename std::enable_if<std::is_base_of<RefObject, T>::value,
    std::shared_ptr<T>>::type
SharedFromThis(T * ptr)
{
    ptr->IncrementRef();
    return std::shared_ptr<T>(ptr, [](T * self){ self->DecrementRef(); });
}

// make_shared
template <typename T, typename ... Args>
typename std::enable_if<std::is_base_of<RefObject, T>::value,
    std::shared_ptr<T>>::type
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

} //namespace co
