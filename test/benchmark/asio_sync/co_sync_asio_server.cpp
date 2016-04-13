#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <sys/resource.h>
#include "coroutine.h"

using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

// 由于socket的析构要依赖于io_service, 所以注意控制
// io_service的生命期要长于socket
io_service ios;
tcp::endpoint addr(address::from_string("127.0.0.1"), 43333);
#ifdef _WIN32
int thread_count = 1;
#else
int thread_count = 4;
#endif
int qdata = 4;
std::atomic<int> session_count{0};

void echo_server()
{
    tcp::acceptor acc(ios, addr, true);
    printf("Corotine-asio server startup, thread:%d, qdata:%d, listen %s:%d\n",
            thread_count, qdata,
            acc.local_endpoint().address().to_string().c_str(),
            acc.local_endpoint().port());
    for (;;) {
        std::shared_ptr<tcp::socket> s(new tcp::socket(ios));
        error_code ec;
        acc.accept(*s, ec);
        if (ec) {
            printf("accept exception %d:%s\n", ec.value(), ec.message().c_str());
            continue;
        }
        go [s]{
            tcp::endpoint addr = s->remote_endpoint();
            ++session_count;
            printf("connected(%d). %s:%d\n", (int)session_count, addr.address().to_string().c_str(), addr.port());
            co_chan<bool> err;

            go [err, s] {
                int buflen = 4096;
                char *buf = new char[buflen];
                std::unique_ptr<char[]> _ep(buf);
                error_code ec;
                int noyield_for_c = 0;
                for (;;++noyield_for_c) {
                    auto n = s->read_some(buffer(buf, buflen), ec);
                    if (ec) {
                        printf("read_some(%d) error %d:%s", s->native_handle(), ec.value(), ec.message().c_str());
                        break;
                    }

                    size_t begin = 0;
goon_write:
                    size_t rn = s->write_some(buffer(buf + begin, std::min<int>(n, qdata)), ec);
                    if (ec) {
                        printf("write_some(%d) error %d:%s", s->native_handle(), ec.value(), ec.message().c_str());
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
                err << true;
            };

            err >> nullptr;
            --session_count;
            printf("disconnected(%d). %s:%d\n", (int)session_count, addr.address().to_string().c_str(), addr.port());
        };
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) 
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("\n    Usage: %s [ThreadCount] [QueryDataLength]\n", argv[0]);
            printf("\n    Default: %s %d 4\n", argv[0], thread_count);
            printf("\n    For example:\n         %s 2 32\n", argv[0]);
            printf("\n    That's means: start server with 2 threads, and per data-package is 32 bytes.\n\n");
            exit(1);
        }

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

