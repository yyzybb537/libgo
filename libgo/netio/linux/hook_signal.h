#pragma once
#include <signal.h>
#include <libgo/config.h>

namespace co
{
    class HookSignal
    {
        HookSignal() = default;
        HookSignal(HookSignal const&) = delete;
        HookSignal(HookSignal &&) = delete;
        HookSignal& operator=(HookSignal const&) = delete;
        HookSignal& operator=(HookSignal &&) = delete;

    public:
        static HookSignal& getInstance();

        sighandler_t SignalSyscall(int signum, sighandler_t handler);

        static void SignalHandler(int signum);

        void Run();

    private:
        std::atomic<uint64_t> sigset_ {0};
        sighandler_t sighandlers_[__SIGRTMAX] = {};
    };

} //namespace co
