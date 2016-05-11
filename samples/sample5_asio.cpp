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

static const uint16_t port = 43333;
using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

// 由于socket的析构要依赖于io_service, 所以注意控制
// io_service的生命期要长于socket
io_service ios;

tcp::endpoint addr(address::from_string("127.0.0.1"), port);

void echo_server()
{
    tcp::acceptor acc(ios, addr, true);
    for (int i = 0; i < 2; ++i) {
        std::shared_ptr<tcp::socket> s(new tcp::socket(ios));
        acc.accept(*s);
        go [s]{
            char buf[1024];
            auto n = s->read_some(buffer(buf));
            n = s->write_some(buffer(buf, n));
            error_code ignore_ec;
            n = s->read_some(buffer(buf, 1), ignore_ec);
        };
    }
}

void client()
{
    tcp::socket s(ios);
    s.connect(addr);
    std::string msg = "1234";
    int n = s.write_some(buffer(msg));
    printf("client send msg [%d] %s\n", (int)msg.size(), msg.c_str());
    char buf[12];
    n = s.receive(buffer(buf, n));
    printf("client recv msg [%d] %.*s\n", n, n, buf);
}

int main()
{
    go echo_server;
    go client;
    go client;

    // 单线程执行
    co_sched.RunUntilNoTask();
    return 0;
}

