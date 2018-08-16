#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "test_server.h"
#include "coroutine.h"
#include <chrono>
#include <time.h>
#include <poll.h>
#include "gtest_exit.h"
using namespace std;
using namespace co;

AsyncCoroutinePool * gPool = AsyncCoroutinePool::Create();

TEST(AsyncPool, AsyncPool)
{
    gPool->InitCoroutinePool(128);
    gPool->Start(4, 12);

    std::atomic<int> val{0};

    AsyncCoroutinePool::CallbackPoint cbPoint;
    cbPoint.SetNotifyFunc([&]{ printf("notified!\n"); ++val; });
    gPool->AddCallbackPoint(&cbPoint);

    uint64_t threadId = NativeThreadID();
    gPool->Post([&]{ 
                ++val; 
                printf("run task\n");
            }, [&]{ 
                ++val; 
                EXPECT_EQ(threadId, NativeThreadID());
                printf("run callback\n");
            });
    while (val != 3)
        cbPoint.Run();
}
