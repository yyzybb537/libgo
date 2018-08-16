#pragma once
#include "../common/config.h"
#include "../common/any.h"
#include "../scheduler/processer.h"
#include "../task/task.h"
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

class CLSMap {
    std::unordered_map<CLSLocation, any, CLSLocation::Hash> map_;

public:
    any& Get(CLSLocation loc) {
        return map_[loc];
    }
};

TaskRefDefine(CLSMap, ClsMap)

extern CLSMap* GetThreadLocalCLSMap();

template <typename T, typename ... Args>
T& GetSpecific(CLSLocation loc, Args && ... args) {
    Task* tk = Processer::GetCurrentTask();
    CLSMap *m = tk ? &TaskRefClsMap(tk) : GetThreadLocalCLSMap();

    any& val = m->Get(loc);
    if (val.empty()) {
//        std::cout << "Set<T> val:" << (void*)&val << ", m:" << (void*)m << std::endl;
//        std::cout << "sizeof:" << sizeof...(args) << std::endl;
        any newVal(T(std::forward<Args>(args)...));
        val.swap(newVal);
    }

    return any_cast<T&>(val);
}

template <typename T>
class CLSRef {
    CLSLocation loc_;
public:
    template <typename ... Args>
    CLSRef(CLSLocation loc, Args && ... args) : loc_(loc) {
        (void)GetSpecific<T>(loc_, std::forward<Args>(args)...);
    }

    operator T const&() const {
        return GetSpecific<T>(loc_);
    }

    operator T&() {
        return GetSpecific<T>(loc_);
    }
};

template <typename T, typename ... Args>
CLSRef<T> MakeCLSRef(CLSLocation loc, Args && ... args) {
    return CLSRef<T>(loc, std::forward<Args>(args)...);
}

#define GetCLSLocation() \
    co::CLSLocation{__LINE__, __COUNTER__, (std::size_t)__FILE__, (std::size_t)__func__}

#define CLS(type, ...) \
    co::MakeCLSRef<type>(GetCLSLocation(), ##__VA_ARGS__)

#define CLS_REF(type) co::CLSRef<type>

} // namespace co
