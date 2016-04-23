#include <boost/thread.hpp>
#include <sys/resource.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <signal.h>
#include "coroutine.h"

static const char* g_ip = "127.0.0.1";
static const uint16_t g_port = 43333;
int thread_count = 4;
int qdata = 4;
std::atomic<int> session_count{0};

void echo_server()
{
    int ret;
    int accept_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(accept_fd >= 0);

    int v = 1;
    ret = setsockopt(accept_fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v));
    assert(ret == 0);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = inet_addr(g_ip);
    ret = bind(accept_fd, (sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    ret = listen(accept_fd, 100);
    assert(ret == 0);

    printf("Coroutine server startup, thread:%d, qdata:%d, listen %s:%d\n",
            thread_count, qdata, g_ip, g_port);
    for (;;) {
        socklen_t addr_len = sizeof(addr);
        int sock_fd = accept(accept_fd, (sockaddr*)&addr, &addr_len);
        if (sock_fd < 0) {
            perror("accept error:");
            continue;
        }

        go [sock_fd]{
            ++session_count;
            printf("connected(%d). socket=%d\n", (int)session_count, sock_fd);
            co_chan<bool> err;

            go [err, sock_fd] {
                char *buf = new char[qdata];
                std::unique_ptr<char[]> _ep(buf);

                int noyield_for_c = 0;
                for (;;++noyield_for_c) {
retry_read:
                    auto n = read(sock_fd, buf, qdata);
                    if (n <= 0) {
                        if (errno == EINTR) {
                            printf("trigger EINTR. socket=%d\n", sock_fd);
                            goto retry_read;
                        }
                        break;
                    }

                    size_t begin = 0;
goon_write:
                    ssize_t rn = write(sock_fd, buf + begin, std::min<int>(n, qdata));
                    if (rn <= 0) {
                        if (errno == EINTR) {
                            printf("trigger EINTR. socket=%d\n", sock_fd);
                            goto goon_write;
                        }
                        break;
                    }
                    n -= rn;
                    begin += rn;
                    if ((noyield_for_c & 0xff) == 0)
                        co_yield;

                    if (n > 0) {
                        ++noyield_for_c;
                        goto goon_write;
                    }
                }
                printf("error trigger, socket=%d. errinfo:%s\n", sock_fd, strerror(errno));
                err << true;
            };

            err >> nullptr;
            --session_count;
            printf("disconnected(%d).\n", (int)session_count);
        };
    }
}

int main(int argc, char **argv)
{
    sigignore(SIGPIPE);

    if (argc > 1) 
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("\n    Usage: %s [ThreadCount] [QueryDataLength]\n", argv[0]);
            printf("\n    Default: %s 4 4\n", argv[0]);
            printf("\n    For example:\n         %s 2 32\n", argv[0]);
            printf("\n    That's means: start server with 2 threads, and per data-package is 32 bytes.\n\n");
            exit(1);
        }

//    co_sched.GetOptions().debug = co::dbg_ioblock;

    if (argc > 1)
        thread_count = atoi(argv[1]);
    if (argc > 2)
        qdata = atoi(argv[2]);

    rlimit of = {65536, 65536};
    if (-1 == setrlimit(RLIMIT_NOFILE, &of)) {
        perror("setrlimit");
        exit(1);
    }

    go echo_server;

    boost::thread_group tg;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([] { g_Scheduler.RunUntilNoTask(); });
    tg.join_all();
    return 0;
}

