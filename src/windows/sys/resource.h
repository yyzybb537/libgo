#pragma once

struct rlimit
{
    int soft_limit;
    int hard_limit;
};

#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE 0
#endif

static inline int setrlimit(int type, rlimit *)
{
    return 0;
}
