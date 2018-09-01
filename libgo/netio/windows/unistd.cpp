#include <unistd.h>
#include <stdint.h>

extern "C" {

    void usleep(uint64_t microseconds)
    {
        ::Sleep((uint32_t)(microseconds / 1000));
    }

    unsigned int sleep(unsigned int seconds)
    {
        ::Sleep(seconds * 1000);
        return seconds;
    }

    int poll(struct pollfd *fds, unsigned long nfds, int timeout)
    {
        return WSAPoll(fds, nfds, timeout);
    }

}