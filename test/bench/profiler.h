#pragma once

#include <gperftools/profiler.h>
#include <signal.h>

class GProfiler
{
public:
    static void Initialize() {
        signal(SIGTTIN, &SignalHandler);
        signal(SIGTTOU, &SignalHandler);
    }

    static void Start() {
        printf("ProfilerStart\n");
        ProfilerStart("/tmp/prof");
    }

    static void Stop() {
        printf("ProfilerStop\n");
        ProfilerStop();
    }

    static void SignalHandler(int sig) {
        static bool profiling = false;

        switch (sig) {
            case SIGTTIN:
                if (!profiling) {
                    profiling = true;
                    Start();
                }
                break;

            case SIGTTOU:
                if (profiling) {
                    profiling = false;
                    Stop();
                }
                break;
        }
    }
};


class GProfilerScope
{
public:
    GProfilerScope() {
        GProfiler::Start();
    }

    ~GProfilerScope() {
        GProfiler::Stop();
    }
};
