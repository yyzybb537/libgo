#pragma once
#include <unordered_map>
#include <typeinfo>
#include <memory>
#include <assert.h>
#include <iostream>

namespace co {

struct CLSLocation {
    int lineno_;
    int counter_;
    std::size_t file_;
    std::size_t func_;
//    const char* class_;

    // if class == nil and func == nil, it's a global variable.
    struct Hash {
        std::size_t operator()(CLSLocation const& loc) const {
            std::size_t h = std::hash<std::size_t>()(loc.file_)
                + std::hash<std::size_t>()(loc.func_)
                + std::hash<int>()(loc.lineno_)
                + std::hash<int>()(loc.counter_);
//            std::cout << "Hash return:" << h << std::endl;
            return h;
        }
    };

    friend bool operator==(CLSLocation const& lhs, CLSLocation const& rhs) {
        bool ret = lhs.lineno_ == rhs.lineno_ &&
            lhs.counter_ == rhs.counter_ &&
            lhs.file_ == rhs.file_ &&
            lhs.func_ == rhs.func_;
//        std::cout << "operator== return:" << ret << std::endl;
        return ret;
    }
};


class CLSAny {
    struct placeholder {
        virtual ~placeholder() {}
        virtual const std::type_info& type() const noexcept = 0;
    };

    template<typename ValueType>
    struct holder : public placeholder
    {
        template <typename ... Args>
        holder(Args && ... args)
          : held(std::forward<Args>(args)...)
        {}

        virtual const std::type_info& type() const noexcept
        {
            return typeid(ValueType);
        }

        ValueType held;
    };

    std::unique_ptr<placeholder> content_;

public:
    bool empty() const {
        return !content_;
    }

    template <typename T, typename ... Args>
    void Set(Args && ... args) {
        std::unique_ptr<placeholder> p(new holder<T>(std::forward<Args>(args)...));
        content_.swap(p);
    }

    template <typename T>
    T& Cast() {
        assert(content_);
        assert(typeid(T) == content_->type());
        return static_cast<holder<T>*>(content_.get())->held;
    }
};

class CLSMap {
    std::unordered_map<CLSLocation, CLSAny, CLSLocation::Hash> map_;
public:
    CLSAny& Get(CLSLocation loc) {
        return map_[loc];
    }
};

} //namespace co
