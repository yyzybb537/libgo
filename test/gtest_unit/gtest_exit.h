#pragma once
#include "libgo.h"
#include <thread>

struct startLibgo {
    startLibgo() {
        std::thread([]{
                g_Scheduler.Start(4, 4);
            }).detach();
    }
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

#ifdef _WIN32
#include <stdlib.h>
struct exit_pause {
	~exit_pause()
	{
		system("pause");
	}
} g_exit;
#endif
