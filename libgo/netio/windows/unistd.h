#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <stdint.h>

typedef int64_t ssize_t;

typedef int socklen_t;

extern "C" {
    void usleep(uint64_t microseconds);

    unsigned int sleep(unsigned int seconds);

    int poll(struct pollfd *fds, unsigned long nfds, int timeout);

}