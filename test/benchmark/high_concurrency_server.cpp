#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include "coroutine.h"

static const char* g_ip = "0.0.0.0";
static const uint16_t g_port = 43333;
int thread_count = 4;
static boost::shared_ptr<int> conn_count_p(new int);
static std::atomic<uint64_t> g_ping;
static std::atomic<uint64_t> g_pong;

#pragma pack(push)
#pragma pack(1)
struct Ping
{
    int next_ping_seconds;
};
struct Pong
{
    int if8 = 0xf8;
};
#pragma pack(pop)

static Pong spong;

void server()
{
    int ret;
    int accept_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(accept_fd >= 0);

    int v = 1;
    ret = setsockopt(accept_fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v));
//    assert(ret == 0);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = inet_addr(g_ip);
    ret = bind(accept_fd, (sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    ret = listen(accept_fd, 8192);
    assert(ret == 0);
    for (;;)
    {
        socklen_t addr_len = sizeof(addr);
        int s = accept(accept_fd, (sockaddr*)&addr, &addr_len);
        if (s < 0) {
            perror("accept error:");
            continue;
        }

        go [s] {
            boost::shared_ptr<int> __cp(conn_count_p);
            char rbuf[128];
            uint32_t rpos = 0;
            int rcv_timeo = 240;
            for (;;)
            {
                timeval tv{rcv_timeo, 0};
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
retry_read:
                //printf("enter ::read\n");
                ssize_t rn = ::read(s, rbuf + rpos, sizeof(rbuf) - rpos);
                //printf("leave ::read\n");
                if (rn < 0) {
                    if (errno == EINTR)
                        goto retry_read;

                    //printf("read error %d:%s\n", errno, strerror(errno));
                    close(s);
                    return ;
                }
                if (rn == 0) {
                    close(s);
                    return ;
                }
                rpos += rn;
                if (rpos < sizeof(Ping))
                    continue;

                uint32_t i = 0;
                while (i + sizeof(Ping) <= rpos)
                {
                    Ping *pping = (Ping*)&rbuf[i];
                    rcv_timeo = htonl(pping->next_ping_seconds);
                    //printf("recv ping(%u) i=%u, rpos=%u\n", rcv_timeo, i, rpos);
                    i += sizeof(Ping);
                    ++g_ping;

retry_write:
                    ssize_t wn = ::write(s, &spong, sizeof(spong));
                    if (wn < 0) {
                        if (errno == EINTR)
                            goto retry_write;

                        //printf("write error %d:%s\n", errno, strerror(errno));
                        close(s);
                        return ;
                    }
                }

                memcpy(&rbuf[0], &rbuf[i], rpos - i);
                rpos -= i;
            }
        };
    }
}

void show_status()
{
    static int s_show_index = 0;
    if (s_show_index++ % 10 == 0) {
        printf("  index    conn \n");
    }

    printf("%6d %6ld\n", s_show_index, conn_count_p.use_count() - 1);
}

int main(int argc, char **argv)
{
    sigignore(SIGPIPE);
    if (argc > 1) 
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("\n    Usage: %s [ThreadCount]\n", argv[0]);
            printf("\n    Default: %s 4\n", argv[0]);
            exit(1);
        }

    if (argc > 1)
        thread_count = atoi(argv[1]);

    rlimit of = {1000000, 1000000};
    if (-1 == setrlimit(RLIMIT_NOFILE, &of)) {
        perror("setrlimit");
        exit(1);
    }

    go server;

    boost::thread_group tg;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([] { g_Scheduler.RunUntilNoTask(); });
    //tg.join_all();
    for (;;)
    {
        sleep(1);
        show_status();
    }
    return 0;
}

