#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <atomic>
#include <sys/resource.h>
using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;
using std::shared_ptr;

// 由于socket的析构要依赖于io_service, 所以注意控制
// io_service的生命期要长于socket
io_service ios;
tcp::endpoint addr(address::from_string("127.0.0.1"), 43333);
std::atomic<int> g_conn{0};
const int g_buflen = 4096;
#ifdef _WIN32
int thread_count = 1;
#else
int thread_count = 4;
#endif
int qdata = 4;

void on_err(shared_ptr<tcp::socket>, char* buf)
{
    delete buf;
    printf("disconnected(%d).\n", --g_conn);
}

void async_read(shared_ptr<tcp::socket> s, char* buf);
void async_write(shared_ptr<tcp::socket> s, char* buf, size_t begin, size_t bytes)
{
    s->async_write_some(buffer(buf + begin, std::min<int>(qdata, bytes)), [=](error_code const& ec, size_t n)mutable {
                if (ec) {
                    on_err(s, buf);
                    return ;
                }

                bytes -= n;
                if (bytes > 0)
                    ::async_write(s, buf, begin + n, bytes);
                else 
                    ::async_read(s, buf);
            });
}

void async_read(shared_ptr<tcp::socket> s, char* buf)
{
    s->async_read_some(buffer(buf, g_buflen), [s, buf](error_code const& ec, size_t n) {
                if (ec) {
                    on_err(s, buf);
                    return ;
                }

                ::async_write(s, buf, 0, n);
            });
}

void on_connected(shared_ptr<tcp::socket> s)
{
    printf("connected(%d).\n", ++g_conn);
    char *buf = new char[g_buflen];
    async_read(s, buf);
}

void accept(shared_ptr<tcp::acceptor> acceptor)
{
    shared_ptr<tcp::socket> s(new tcp::socket(ios));
    acceptor->async_accept(*s, [acceptor, s](error_code const& ec) {
                accept(acceptor);
                if (!ec)
                    on_connected(s);
                else
                    printf("accept error:%s\n", ec.message().c_str());
            });
}

void echo_server()
{
    shared_ptr<tcp::acceptor> acceptor(new tcp::acceptor(ios, addr, true));
    accept(acceptor);
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

    printf("startup server, thread:%d, qdata:%d, listen %s:%d\n", thread_count,
            qdata, addr.address().to_string().c_str(), addr.port());

    echo_server();

    boost::thread_group tg;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([]{ ios.run(); });
    tg.join_all();
    return 0;
}

