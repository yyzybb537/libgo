#include <boost/thread.hpp>
#include <sys/resource.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <signal.h>
#include "coroutine.h"
using namespace std::chrono;

static const char* g_ip = "127.0.0.1";
static const uint16_t g_port = 43333;

std::atomic<long unsigned> g_sendbytes{0}, g_recvbytes{0};
std::atomic<int> session_count{0};
uint32_t qdata = 4;
int thread_count = 4;

void client()
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket create error:");
    }
    assert(sock_fd >= 0);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = inet_addr(g_ip);
    int ret = connect(sock_fd, (sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        ++session_count;

        char *buf = new char[qdata];
        std::unique_ptr<char[]> _ep(buf);
        for (;;) {
            ssize_t n = write(sock_fd, buf, qdata);
            if (n <= 0) break;
            g_sendbytes += n;

            n = read(sock_fd, buf, qdata);
            if (n <= 0) break;
            g_recvbytes += n;
        }

        --session_count;
    }

    // 断线以后, 创建新的协程去连接.
    go client;
}

void show_status()
{
    static int show_title = 0;
    static long unsigned last_sendbytes = 0, last_recvbytes = 0;
    static auto start_time = system_clock::now();
    static auto last_time = system_clock::now();
    auto now = system_clock::now();
    if (show_title++ % 10 == 0) {
        printf("thread:%d, qdata:%d\n", thread_count, qdata);
        printf("  conn   send(KB)   recv(KB)     qps   AverageQps  time_delta(ms)\n");
    }
    printf("%6d  %9lu  %9lu  %7d  %7d    %7d\n",
            (int)session_count, (g_sendbytes - last_sendbytes) / 1024, (g_recvbytes - last_recvbytes) / 1024,
            (int)((double)(g_recvbytes - last_recvbytes) / qdata),
            (int)((double)g_recvbytes / qdata / std::max<int>(1, duration_cast<seconds>(now - start_time).count() + 1)),
            (int)duration_cast<milliseconds>(now - last_time).count()
            );
    last_time = now;
    last_sendbytes = g_sendbytes;
    last_recvbytes = g_recvbytes;
}

int main(int argc, char **argv)
{
    sigignore(SIGPIPE);
    if (argc > 1) 
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("\n    Usage: %s [ThreadCount] [Connection_Count] [QueryDataLength]\n", argv[0]);
            printf("\n    Default: %s 4 1024 4\n", argv[0]);
            printf("\n    For example:\n        %s 2 1000 32\n", argv[0]);
            printf("\n    That's means: start client with 2 threads, create 1000 tcp connection to server, and per data-package is 32 bytes.\n\n");
            exit(1);
        }

    int conn_count = 1024;
    if (argc > 1)
        thread_count = atoi(argv[1]);
    if (argc > 2)
        conn_count = atoi(argv[2]);
    if (argc > 3)
        qdata = atoi(argv[3]);

    rlimit of = {65536, 65536};
    if (-1 == setrlimit(RLIMIT_NOFILE, &of)) {
        perror("setrlimit");
        exit(1);
    }

    for (int i = 0; i < conn_count; ++i)
        go client;

    boost::thread_group tg;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([] { g_Scheduler.RunUntilNoTask(); });
    for (;;) {
        sleep(1);
        show_status();
    }
    return 0;
}

