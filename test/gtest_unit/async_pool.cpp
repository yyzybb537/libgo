#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include <time.h>
#include <poll.h>
#include "gtest_exit.h"
using namespace std;
using namespace co;

AsyncCoroutinePool * gPool = AsyncCoroutinePool::Create();

struct R
{
    R() = default;
    explicit R(int v) : val_(v) {}
    R(R &&) = default;
    R& operator=(R &&) = default;

    R(R const&) {
        printf("R copyed.\n");
    }
    R& operator=(R const&) = delete;

    int val_;
};

R calc() {
    return R(8);
}

void cb1(R val) {
    printf("callback by value, val = %d\n", val.val_);
}

void cb2(R & val) {
    printf("callback by reference, val = %d\n", val.val_);
}

TEST(AsyncPool, AsyncPool)
{
    gPool->InitCoroutinePool(128);
    gPool->Start(4, 12);

    std::atomic<int> val{0};

    AsyncCoroutinePool::CallbackPoint *cbPoint = new AsyncCoroutinePool::CallbackPoint;
    cbPoint->SetNotifyFunc([&]{ 
//            printf("notified!\n");
            ++val;
            });
    gPool->AddCallbackPoint(cbPoint);

    gPool->Post<R>(&calc, &cb1);
    gPool->Post<R>(&calc, &cb2);

    uint64_t threadId = NativeThreadID();
    const int c = 100;
    for (int i = 0; i < c; ++i)
        gPool->Post([&]{ 
                    ++val; 
//                    printf("run task\n");
                }, [&]{ 
                    ++val; 
                    EXPECT_EQ(threadId, NativeThreadID());
//                    printf("run callback\n");
                });
    while (val != c * 3 + 2)
        cbPoint->Run();
}
