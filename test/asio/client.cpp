#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <libgo/libgo.h>
#include <fast_asio/fast_asio.hpp>
#include <atomic>
#include <chrono>
#include <benchmark/stat.hpp>
using namespace std;

#define OUT(x) cout << #x << " = " << x << endl
#define O(x) cout << x << endl

using namespace boost::asio;
using namespace boost::asio::ip;

// socket type
typedef fast_asio::packet_stream<tcp::socket> socket_t;
typedef std::shared_ptr<socket_t> socket_ptr;

enum class eSendType {
    null,
    coroutine,
    post,
};

static const eSendType c_send_type = eSendType::null;
io_context poster;

static const int concurrency = 100;
static std::chrono::system_clock::time_point sendTicks[concurrency] = {};
static std::chrono::nanoseconds maxLatency[concurrency] = {};

void onSendPacket(int idx) {
    assert(idx >= 0 && idx < concurrency);
    sendTicks[idx] = std::chrono::system_clock::now();
}

void onReceivePacket(int idx) {
//    printf("idx=%d\n", idx);
    assert(idx >= 0 && idx < concurrency);
    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now() - sendTicks[idx]);
    maxLatency[idx] = std::max(maxLatency[idx], latency);
}

string onShow() {
    std::chrono::nanoseconds latency{};
    for (int i = 0; i < concurrency; ++i) {
        if (maxLatency[i] > latency)
            latency = maxLatency[i];
        maxLatency[i] = std::chrono::nanoseconds();
    }
    char buf[128];
    int len = 0;
    long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(latency).count();
    long us = std::chrono::duration_cast<std::chrono::microseconds>(latency).count();
    long ms = std::chrono::duration_cast<std::chrono::milliseconds>(latency).count();
    if (ms > 0)
        len = snprintf(buf, sizeof(buf), "max latency: %ld ms", ms);
    else if (us > 0)
        len = snprintf(buf, sizeof(buf), "max latency: %ld us", us);
    else
        len = snprintf(buf, sizeof(buf), "max latency: %ld ns", ns);
    return string(buf, len);
}

void onReceive(socket_ptr socket, boost::system::error_code ec, const_buffer* buf_begin, const_buffer* buf_end) {
    if (ec) {
        std::cout << "disconnected, reason:" << ec.message() << std::endl;
        return ;
    }

    size_t bytes = 0;
    for (auto it = buf_begin; it != buf_end; ++it)
        bytes += it->size();

    stats::instance().inc(stats::qps, 1);
    stats::instance().inc(stats::bytes, bytes);

    // ping-pong
    for (auto it = buf_begin; it != buf_end; ++it) {
        string *buf = new string((char*)it->data(), it->size());
        int idx = *(int*)(buf->data() + 4);
        onReceivePacket(idx);
        onSendPacket(idx);
        if (c_send_type == eSendType::coroutine) {
            go [socket, buf, idx] {
                socket->async_write_some(boost::asio::buffer(*buf));
                delete buf;
            };
        } else if (c_send_type == eSendType::post){
            poster.post([socket, buf, idx] {
                socket->async_write_some(boost::asio::buffer(*buf));
                delete buf;
            });
        } else {
            socket->async_write_some(boost::asio::buffer(*buf));
            delete buf;
        }
    }

    socket->async_read_some(std::bind(&onReceive, socket,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));
}

int main() {
    stats::instance().set_message_functor(&onShow);

    io_context ioc;

    socket_ptr socket(new socket_t(ioc));

    // 1.设置拆包函数 (默认函数就是这个, 可以不写这一行)
    socket->set_packet_splitter(&fast_asio::default_packet_policy::split);

    // 2.连接
    tcp::endpoint addr(address::from_string("127.0.0.1"), 1234);
    socket->next_layer().async_connect(addr,
            [socket](boost::system::error_code ec) {
                if (ec) {
                    std::cout << "connect error:" << ec.message() << std::endl;
                    return ;
                }

                std::cout << "connect success" << std::endl;

                // 3.连接成功, 发起读操作
                socket->async_read_some(std::bind(&onReceive, socket,
                            std::placeholders::_1,
                            std::placeholders::_2,
                            std::placeholders::_3));

                // 4.发一个包
                char buf[15] = {};
                for (int i = 0; i < concurrency; ++i) {
                    *(int*)(&buf[0]) = i;
                    std::string packet = fast_asio::default_packet_policy::serialize_to_string(buffer(buf, sizeof(buf)));
                    onSendPacket(i);
                    socket->async_write_some(buffer(packet), [](boost::system::error_code ec, size_t){
//                                std::cout << "ping " << ec.message() << std::endl;
                            });
                }
            });

    co_sched.goStart();
    std::thread([&] {
            boost::asio::io_service::work w(poster);
            poster.run();
            }).detach();

    boost::asio::io_service::work w(ioc);
    ioc.run();
}

