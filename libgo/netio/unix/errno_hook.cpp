#include "errno_hook.h"

#if defined(LIBGO_SYS_Unix)
#include <stdio.h>

extern int *__errno_location (void);
extern "C" volatile int * libgo__errno_location (void)
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
//    printf("hook errno in libgo.\n");
    return (volatile int*)__errno_location();
}
#endif
