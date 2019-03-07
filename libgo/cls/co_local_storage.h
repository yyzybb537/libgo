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

typedef int* CLSLocation;

class CLSMap {
    std::unordered_map<CLSLocation, any> map_;

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
          
          return val.emplace<T>(std::forward<Args>(args)...);
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

#define GetCLSLocation() []{ static int dummy = 0; return &dummy; }()

#define CLS(type, ...) \
    co::MakeCLSRef<type>(GetCLSLocation(), ##__VA_ARGS__)

#define CLS_REF(type) co::CLSRef<type>

} // namespace co
