#pragma once
#include <libgo/config.h>
#include <libgo/scheduler.h>
#include <iostream>

namespace co {

extern CLSMap* GetThreadLocalCLSMap();

template <typename T, typename ... Args>
T& GetSpecific(CLSLocation loc, Args && ... args) {
    Task* task = Scheduler::getInstance().GetCurrentTask();
    CLSMap *m = nullptr;
    if (!task) {
        m = GetThreadLocalCLSMap();
    } else {
        m = task->GetCLSMap();
    }

    CLSAny& any = m->Get(loc);
    if (any.empty()) {
//        std::cout << "Set<T> any:" << (void*)&any << ", m:" << (void*)m << std::endl;
//        std::cout << "sizeof:" << sizeof...(args) << std::endl;
        any.Set<T>(std::forward<Args>(args)...);
    }
    return any.Cast<T>();
}

template <typename T>
class CLSRef {
    CLSLocation loc_;
public:
    template <typename ... Args>
    CLSRef(CLSLocation loc, Args && ... args) : loc_(loc) {
        (void)GetSpecific<T>(loc_, std::forward<Args>(args)...);
    }

    operator T&() const {
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
//    co::GetSpecific<type>(GetCLSLocation(), ##__VA_ARGS__)

#define CLS_REF(type) co::CLSRef<type>

} // namespace co
