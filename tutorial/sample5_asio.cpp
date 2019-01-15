/************************************************
 * libgo sample5
*************************************************/

/***********************************************
 * 结合boost.asio, 使网络编程变得更加简单.
 * 如果你不喜欢boost.asio, 这个例子可以跳过不看.
************************************************/
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include "coroutine.h"
#include "win_exit.h"

// 设置网络线程数 (仅支持linux & mac)
#if defined(LIBGO_SYS_Unix)
#include "netio/unix/reactor.h"
#endif

static const uint16_t port = 43332;
using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

io_service ios;

void echo_server(tcp::endpoint const& addr)
{
    tcp::acceptor acc(ios, addr, true);
    for (int i = 0; i < 2; ++i) {
        std::shared_ptr<tcp::socket> s(new tcp::socket(ios));
        error_code ec;
        acc.accept(*s, ec);
        if (ec) {
            printf("line:%d accept error:%s\n", __LINE__, ec.message().c_str());
            return;
        }

        go [s]{
            char buf[1024];
            error_code ec;
            auto n = s->read_some(buffer(buf), ec);
            if (ec) {
                printf("line:%d read_some error:%s\n", __LINE__, ec.message().c_str());
                return;
            }

            n = s->write_some(buffer(buf, n), ec);
            if (ec) {
                printf("line:%d write_some error:%s\n", __LINE__, ec.message().c_str());
                return;
            }

            error_code ignore_ec;
            n = s->read_some(buffer(buf, 1), ignore_ec);
        };
    }
}

void client(tcp::endpoint const& addr)
{
    tcp::socket s(ios);
    printf("start connect\n");
    error_code ec;
    s.connect(addr, ec);
    if (ec) {
        printf("line:%d connect error:%s\n", __LINE__, ec.message().c_str());
        return;
    }

    printf("connected success\n");

    std::string msg = "1234";
    int n = s.write_some(buffer(msg), ec);
    if (ec) {
        printf("line:%d write_some error:%s\n", __LINE__, ec.message().c_str());
        return;
    }

    printf("client send msg [%d] %s\n", (int)msg.size(), msg.c_str());
    char buf[12];
    n = s.receive(buffer(buf, n));
    printf("client recv msg [%d] %.*s\n", n, n, buf);
}

int main()
{
    // 设置网络线程数 (仅支持linux & mac)
#if defined(LIBGO_SYS_Unix)
    co::Reactor::InitializeReactorCount(2);
#endif

    for (int i = 0; i < 5; ++i) {
        tcp::endpoint addr(address::from_string("127.0.0.1"), port + i);
        go [addr]{ echo_server(addr); };
        go [addr]{ client(addr); };
    }

    // 200ms后安全退出
    std::thread([]{ co_sleep(200); co_sched.Stop(); }).detach();

    // 单线程执行
    co_sched.Start();
    return 0;
}

