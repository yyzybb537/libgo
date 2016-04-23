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

static const char* g_ip = "127.0.0.1";
static const uint16_t g_port = 43333;
int thread_count = 4;
int client_count = 50000;
static boost::shared_ptr<int> conn_count_p(new int);
static std::atomic<uint64_t> g_ping;
static std::atomic<uint64_t> g_pong;

#pragma pack(push)
#pragma pack(1)
struct Ping
{
    int next_ping_seconds;
    Ping() {
        next_ping_seconds = htonl(180);
    }
};
struct Pong
{
    int if8 = 0xf8;
};
#pragma pack(pop)

static Ping sping;

void client();
struct client_end
{
    int s_;
    client_end(int s) : s_(s) {}
    ~client_end() {
        close(s_);
        go client;
    }
};

void client()
{
    int ret;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);

    client_end go_on(s);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = inet_addr(g_ip);
    ret = ::connect(s, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        printf("connect error %d:%s\n", errno, strerror(errno));
        return ;
    }
    boost::shared_ptr<int> __cp(conn_count_p);

    for (;;)
    {
        co_sleep(180 * 1000);
        //co_sleep(180);
        ssize_t wpos = 0;
retry_write:
        ssize_t wn = ::write(s, (char*)&sping + wpos, sizeof(sping) - wpos);
        if (wn < 0) {
           if (errno == EINTR)
               goto retry_write;
           else
               return ;
        }
        wpos += wn;
        if ((std::size_t)wpos < sizeof(sping))
            goto retry_write;
        printf("ping\n");

        Pong pong;
        ssize_t rpos = 0;
retry_read:
        ssize_t rn = ::read(s, (char*)&pong + rpos, sizeof(pong) - rpos);
        if (rn < 0) {
            if (errno == EINTR)
                goto retry_read;
            else
                return ;
        }
        if (rn == 0) {
            return ;
        }
        rpos += rn;
        if ((std::size_t)rpos < sizeof(pong))
            goto retry_read;
        printf("pong\n");
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
            printf("\n    Usage: %s [ip] [ThreadCount] [ClientCount]\n", argv[0]);
            printf("\n    Default: %s 127.0.0.1 4 50000\n", argv[0]);
            exit(1);
        }

    if (argc > 1)
        g_ip = argv[1];
    if (argc > 2)
        thread_count = atoi(argv[2]);
    if (argc > 3)
        client_count = atoi(argv[3]);

    rlimit of = {100000, 100000};
//    rlimit of = {RLIM_INFINITY, RLIM_INFINITY};
    if (-1 == setrlimit(RLIMIT_NOFILE, &of)) {
        perror("setrlimit");
        exit(1);
    }

    for (int i = 0; i < client_count; ++i)
        go client;

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

