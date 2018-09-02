#pragma once
#include "../gtest_exit.h"
#include "libgo/coroutine.h"
#include <boost/any.hpp>
#include <unordered_map>
#include <iostream>

typedef std::unordered_map<int, boost::any> Storage;

inline Storage & getStorage() {
    static Storage obj;
    return obj;
}

template <typename T>
inline void setHookVal(T t, int line) {
    getStorage()[line] = boost::any(t);
}

template <typename T>
inline T* getHookVal(T t, int line) {
    boost::any & any = getStorage()[line];
    if (any.empty()) return nullptr;
    return boost::any_cast<T>(&any);
}

#define HOOK_EQ(x) do { \
        CHECK_POINT(); \
        if (!::co::Processer::IsCoroutine()) { \
            setHookVal(x, __LINE__); \
        } else { \
            auto ptr = getHookVal(x, __LINE__); \
            EXPECT_TRUE(!!ptr); \
            if (ptr) { \
                EXPECT_EQ(x, *ptr); \
                if (x != *ptr) { \
                    std::cout << "Origin:" << *ptr << ", Hook:" << x << std::endl; \
                } \
            } \
        } \
    } while (0)

inline int fill_send_buffer(int fd)
{
    const int fillSlice = 1024;
    static char* buf = new char[fillSlice];
    int c = 0;
    for (;;) {
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 1000) <= 0)
            break;
        c += ::send(fd, buf, fillSlice, 0);
//        printf("fill %d bytes\n", c);
        CHECK_POINT();
    }
    return c;
}
