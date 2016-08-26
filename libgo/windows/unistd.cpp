#include <unistd.h>
#include <Windows.h>
#include <stdint.h>

void usleep(uint64_t microseconds)
{
    ::Sleep((uint32_t)(microseconds / 1000));
}

unsigned int sleep(unsigned int seconds)
{
    ::Sleep(seconds * 1000);
    return seconds;
}
