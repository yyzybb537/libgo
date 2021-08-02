#pragma once

//#if 0
#if defined(LIBGO_SYS_Unix)
#include "../../common/config.h"
#include <errno.h>
#include <stdio.h>

# ifdef errno
extern int *__errno_location (void);
extern "C" volatile int * my__errno_location (void) __attribute__((noinline));

extern "C" volatile int * my__errno_location (void)
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
//    printf("hook errno in libgo.\n");
    return (volatile int*)__errno_location();
}
#  undef errno
#  define errno (*my__errno_location())
# endif

#endif
//#endif
