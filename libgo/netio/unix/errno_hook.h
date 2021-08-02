#pragma once
#include "../../common/config.h"

//#if 0
#if defined(LIBGO_SYS_Unix)
#include <errno.h>

# ifdef errno
extern "C" volatile int * libgo__errno_location (void) __attribute__((noinline));
#  undef errno
#  define errno (*libgo__errno_location())
# endif

#endif
//#endif
