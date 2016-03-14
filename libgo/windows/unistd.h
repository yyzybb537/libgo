#pragma once
#include <stdint.h>

typedef int64_t ssize_t;

void usleep(uint64_t microseconds);

unsigned int sleep(unsigned int seconds);
