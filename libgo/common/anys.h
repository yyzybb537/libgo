#pragma once
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
            new (reinterpret_cast<T*>(ptr)) T;
        }
        inline static void Destructor(void *ptr) {
            reinterpret_cast<T*>(ptr)->~T();
        }
    };

    // 注册一个存储到Task中的kv, 返回取数据用的index
    // 注册行为必须在创建第一个Task之前全部完成, 建议在全局对象初始化阶段完成
    template <typename T>
    static std::size_t Register(std::string keyName)
    {
        return Register(keyName, &DefaultConstructorDestructor<T>::Constructor, &DefaultConstructorDestructor<T>::Destructor);
    }

    template <typename T>
    static std::size_t Register(std::string keyName, Constructor constructor, Destructor destructor)
    {
        std::unique_lock<std::mutex> lock(GetMutex());

        KeyInfo info;
        info.align = std::alignment_of<T>::value;
        info.size = sizeof(T);
        info.offset = StorageLen();
        info.constructor = constructor;
        info.destructor = destructor;
        info.keyName = keyName;
        GetKeys().push_back(info);
        StorageLen() += info.align + info.size - 1;
        Size()++;
        return GetKeys().size() - 1;
    }

    template <typename T>
    T& get(std::size_t index)
    {
        if (index >= Size()) {
            throw std::logic_error("Anys::get overflow");
        }

        auto const& keyInfo = GetKeys()[index];
        std::size_t offset = keyInfo.offset;
        std::size_t space = keyInfo.align + keyInfo.size - 1;
        void *p = storage_ + offset;
        if (!std::align(keyInfo.align, keyInfo.size, p, space))
            throw std::logic_error("Anys::get call std::align error");

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
        std::string keyName;
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

private:
    // TODO: 优化:紧凑内存布局, 提高利用率
    char* storage_;

public:
    Anys()
        : storage_(nullptr)
    {
        if (!Size()) return ;
        storage_ = (char*)malloc(StorageLen());
        Init();
    }

    ~Anys()
    {
        Deinit();
        if (storage_) {
            free(storage_);
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

            std::size_t offset = keyInfo.offset;
            std::size_t space = keyInfo.align + keyInfo.size - 1;
            void *p = storage_ + offset;
            if (!std::align(keyInfo.align, keyInfo.size, p, space))
                throw std::logic_error("Anys::get call std::align error");
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

            std::size_t offset = keyInfo.offset;
            std::size_t space = keyInfo.align + keyInfo.size - 1;
            void *p = storage_ + offset;
            if (!std::align(keyInfo.align, keyInfo.size, p, space))
                throw std::logic_error("Anys::get call std::align error");
            keyInfo.destructor(p);
        }
    }
};

} //namespace co
