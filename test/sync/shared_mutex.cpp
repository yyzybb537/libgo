#include <iostream>
#include <iomanip>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <functional>
#include <libgo.h>
#include <shared_mutex>
using namespace std;
using namespace std::chrono;

long nowTime()
{
    static auto begin = std::chrono::steady_clock::now();
    auto tp = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<
        std::chrono::milliseconds>(tp - begin).count();
}

bool check(long* arr, int len, int timeout, const char* loginfo)
{
    long now = nowTime();
    for (int i = 0; i < len; ++i) {
        long t = arr[i];
        if (!t)
            continue;

        if (t + timeout < now)
        {
            printf("check error. [%s] i=%d\n", loginfo, i);
            return false;
        }
    }

    return true;
}

TEST(sync, test_shared_mutex)
{
    co_rwmutex smtx;
    int n = 0;
    bool quit = false;

    const int wc = 8, rc = 16;
    long write_ticks[wc] = {};
    long read_ticks[rc] = {};

    // write
    for (int i = 0; i < 8; ++i) {
        go [&, i] {
            while (!quit) {
                write_ticks[i] = nowTime();
                std::unique_lock<co_rwmutex> lock(smtx);
                write_ticks[i] = 0;
                n++;

                co_sleep(10);
            }
        };
    }

    // read
    for (int i = 0; i < 16; ++i) {
        go [&, i] {
            while (!quit) {
                read_ticks[i] = nowTime();
                std::shared_lock<co_rwmutex> slock(smtx);
//                std::unique_lock<co_rwmutex> lock(smtx);
                read_ticks[i] = 0;
                int c = n;
                (void)c;

                co_sleep(10);
            }
        };
    }

    sleep(1);
    bool ok = true;
    for (int i = 0; i < 3600; ++i) {
        // check
        ok = check(write_ticks, wc, 1000, "write") && ok;
        ok = check(read_ticks, rc, 1000, "read") && ok;
        sleep(1);
//        printf("tick: %ld\n", nowTime());
    }

    quit = true;

    if (!ok) {
        printf("check error.\n");
        sleep(6000);
    } else {
        printf("success.\n");
        sleep(1);
    }
}
