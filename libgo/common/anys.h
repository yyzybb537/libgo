#pragma once
#include "config.h"
#include "spinlock.h"
#include <string>
#include <vector>
#include <type_traits>
#include <mutex>
#include <assert.h>
#include <memory>

namespace co
{

template <typename Group>
class Anys
{
public:
    typedef void (*Constructor)(void*);
    typedef void (*Destructor)(void*);

    template <typename T>
    struct DefaultConstructorDestructor
    {
        inline static void Constructor(void *ptr) {
            new (reinterpret_cast<T*>(ptr)) T();
        }
        inline static void Destructor(void *ptr) {
            reinterpret_cast<T*>(ptr)->~T();
        }
    };

    // 注册一个存储到Task中的kv, 返回取数据用的index
    // 注册行为必须在创建第一个Task之前全部完成, 建议在全局对象初始化阶段完成
    template <typename T>
    static std::size_t Register()
    {
        return Register<T>(&DefaultConstructorDestructor<T>::Constructor, &DefaultConstructorDestructor<T>::Destructor);
    }

    template <typename T>
    static std::size_t Register(Constructor constructor, Destructor destructor)
    {
        std::unique_lock<std::mutex> lock(GetMutex());
        std::unique_lock<LFLock> inited(GetInitGuard(), std::defer_lock);
        if (!inited.try_lock())
            throw std::logic_error("Anys::Register mustbe at front of new first instance.");

        KeyInfo info;
        info.align = std::alignment_of<T>::value;
        info.size = sizeof(T);
        info.offset = StorageLen();
        info.constructor = constructor;
        info.destructor = destructor;
        GetKeys().push_back(info);
        StorageLen() += info.align + info.size - 1;
        Size()++;
        return GetKeys().size() - 1;
    }

    template <typename T>
    ALWAYS_INLINE T& get(std::size_t index)
    {
        if (index >= *(std::size_t*)hold_)
            throw std::logic_error("Anys::get overflow");

        char *p = storage_ + offsets_[index];
        return *reinterpret_cast<T*>(p);
    }

private:
    struct KeyInfo
    {
        int align;
        int size;
        std::size_t offset;
        Constructor constructor;
        Destructor destructor;
    };
    inline static std::vector<KeyInfo> & GetKeys()
    {
        static std::vector<Anys::KeyInfo> obj;
        return obj;
    }
    inline static std::mutex & GetMutex()
    {
        static std::mutex obj;
        return obj;
    }
    inline static std::size_t & StorageLen()
    {
        static std::size_t obj = 0;
        return obj;
    }
    inline static std::size_t & Size()
    {
        static std::size_t obj = 0;
        return obj;
    }
    inline static LFLock & GetInitGuard()
    {
        static LFLock obj;
        return obj;
    }

private:
    // TODO: 优化:紧凑内存布局, 提高利用率
    char* hold_;
    std::size_t* offsets_;
    char* storage_;

public:
    Anys()
        : hold_(nullptr), offsets_(nullptr), storage_(nullptr)
    {
        GetInitGuard().try_lock();
        if (!Size()) return ;
        hold_ = (char*)malloc(sizeof(std::size_t) * (Size() + 1) + StorageLen());
        storage_ = hold_ + sizeof(std::size_t) * (Size() + 1);
        *(std::size_t*)hold_ = Size();
        offsets_ = (std::size_t*)(hold_ + sizeof(std::size_t));
        for (std::size_t i = 0; i < Size(); i++) {
            auto const& keyInfo = GetKeys()[i];
            std::size_t offset = keyInfo.offset;
            std::size_t space = keyInfo.align + keyInfo.size - 1;
            char *base = storage_ + offset;
            void *ptr = base;
            if (!align(keyInfo.align, keyInfo.size, ptr, space))
                throw std::logic_error("Anys::get call std::align error");
            offset += (char*)ptr - base;
            offsets_[i] = offset;
        }
        Init();
    }

    ~Anys()
    {
        Deinit();
        if (hold_) {
            free(hold_);
            hold_ = nullptr;
            offsets_ = nullptr;
            storage_ = nullptr;
        }
    }

    void Reset()
    {
        Deinit();
        Init();
    }

    void Init()
    {
        if (!Size()) return ;
        for (std::size_t i = 0; i < Size(); i++)
        {
            auto const& keyInfo = GetKeys()[i];
            if (!keyInfo.constructor)
                continue;

            char *p = storage_ + offsets_[i];
            keyInfo.constructor(p);
        }
    }

    void Deinit()
    {
        if (!Size()) return ;
        for (std::size_t i = 0; i < Size(); i++)
        {
            auto const& keyInfo = GetKeys()[i];
            if (!keyInfo.destructor)
                continue;

            char *p = storage_ + offsets_[i];
            keyInfo.destructor(p);
        }
    }

    // Copy from g++ std.
    inline void* align(size_t __align, size_t __size, void*& __ptr, size_t& __space) noexcept
    {
        const auto __intptr = reinterpret_cast<uintptr_t>(__ptr);
        const auto __aligned = (__intptr - 1u + __align) & -__align;
        const auto __diff = __aligned - __intptr;
        if ((__size + __diff) > __space)
            return nullptr;
        else
        {
            __space -= __diff;
            return __ptr = reinterpret_cast<void*>(__aligned);
        }
    }
};

} //namespace co
