/************************************************
 * cpu.test
*************************************************/
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include "coroutine.h"
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

static const uint16_t port = 43333;

bool is_exit = false;
void signal_hd(int signo)
{
    if (signo == SIGUSR1)
        is_exit = true;
}

using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

io_service ios;

tcp::endpoint addr(address::from_string("127.0.0.1"), port);

void echo_server()
{
    tcp::acceptor acc(ios, addr, true);
    for (;;) {
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

int main(int argc, char** argv)
{
    signal(SIGUSR1, signal_hd);

    int thread_count = 1;
    if (argc > 1) {
        thread_count = atoi(argv[1]);
    }
    printf("thread_count=%d\n", thread_count);

//    g_Scheduler.GetOptions().debug = dbg_sleep;

    go echo_server;

    // 单线程执行
    boost::thread_group tg;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([&] { while (!is_exit) g_Scheduler.Run();} );
    tg.join_all();
    return 0;
}

