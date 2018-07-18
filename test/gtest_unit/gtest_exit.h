#pragma once
#include "../../libgo/libgo.h"
#include <thread>
#include "gtest/gtest.h"

#ifndef TEST_MIN_THREAD
#define TEST_MIN_THREAD 4
#endif

#ifndef TEST_MAX_THREAD
#define TEST_MAX_THREAD 4
#endif

struct startLibgo {
    startLibgo() {
        pThread = new std::thread([]{
                g_Scheduler.Start(TEST_MIN_THREAD, TEST_MAX_THREAD);
            });
    }
    std::thread *pThread;
};

startLibgo __startLibgo;

inline void __WaitUntilNoTask(int line, std::size_t val = 0) {
    int i = 0;
    while (g_Scheduler.TaskCount() > val) {
        usleep(1000);
        if (++i % 3000 == 0) {
            printf("LINE: %d, TaskCount: %d\n", line, (int)g_Scheduler.TaskCount());
        }
    }
}

#define WaitUntilNoTask(n, ...) __WaitUntilNoTask(__LINE__, ##__VA_ARGS__)

inline void DumpTaskCount() {
    printf("TaskCount: %d\n", (int)g_Scheduler.TaskCount());
}

struct GTimer {
    GTimer() : tp_(co::FastSteadyClock::now()) {}

    co::FastSteadyClock::duration duration() {
        return co::FastSteadyClock::now() - tp_;
    }

    int ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                co::FastSteadyClock::now() - tp_).count();
    }

private:
    co::FastSteadyClock::time_point tp_;
};

#define DEFAULT_DEVIATION 50
#define TIMER_CHECK(t, val, deviation) \
        do { \
            auto c = t.ms(); \
            EXPECT_GT(c, val - 1); \
            EXPECT_LT(c, val + deviation); \
        } while (0)

#ifdef _WIN32
#include <stdlib.h>
struct exit_pause {
	~exit_pause()
	{
		system("pause");
	}
} g_exit;
#endif
