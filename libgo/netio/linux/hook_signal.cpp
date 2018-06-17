#include "hook_signal.h"
#include <libgo/config.h>

namespace co
{
    HookSignal & HookSignal::getInstance()
    {
        static HookSignal obj;
        return obj;
    }
    sighandler_t HookSignal::SignalSyscall(int signum, sighandler_t handler)
    {
        DebugPrint(dbg_signal, "SignalSyscall %d handler=%p", signum, (void*)handler);
        if (signum > __SIGRTMAX) return SIG_IGN;

        sighandler_t old = sighandlers_[signum];
        sighandlers_[signum] = handler;
        if (handler == SIG_IGN || handler == SIG_DFL)
            sysv_signal(signum, handler);
        else
            sysv_signal(signum, &HookSignal::SignalHandler);
        return old;
    }
    void HookSignal::SignalHandler(int signum)
    {
//        DebugPrint(dbg_signal, "SignalHandler %d start", signum);
        getInstance().sigset_ |= (uint64_t)1 << signum;
        sysv_signal(signum, &HookSignal::SignalHandler);
//        DebugPrint(dbg_signal, "SignalHandler %d end", signum);
    }
    void HookSignal::Run()
    {
        uint64_t ss = std::atomic_exchange(&sigset_, (uint64_t)0);
        if (!ss) return ;

        for (int i = 0; i < 64 && ss; ++i)
        {
            if (ss & ((uint64_t)1 << i)) {
                ss &= ~((uint64_t)1 << i);
                DebugPrint(dbg_signal, "Signal Trigger %d", i);

                sighandler_t handler = sighandlers_[i];
                if (handler != SIG_IGN && handler != SIG_DFL)
                    handler(i);
            }
        }
    }

} //namespace co
