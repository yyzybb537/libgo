#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/smart_ptr/shared_array.hpp>
#include <assert.h>
using ::boost::asio::io_service;
using ::boost::asio::buffer;
using namespace ::boost::asio::ip;
using ::boost::system::error_code;

#define assert_false(expr) assert(!(expr))
#define TEST_PORT 43222

struct TestServer
{
    io_service &ios_;
    tcp::acceptor *accept_ = NULL;
    bool is_work_;
    boost::thread work_thread_;
    int count_ = 0;

    static io_service& get_io_service()
    {
        static io_service ios;
        return ios;
    }

    TestServer() : ios_(get_io_service()), is_work_(true)
    {
#ifndef WINNT
        rlimit of = {4096, 4096};
        if (-1 == setrlimit(RLIMIT_NOFILE, &of)) {
            fprintf(stderr, "setrlimit error %d:%s\n", errno, strerror(errno));
            exit(1);
        }
#endif //WINNT

        tcp::endpoint addr(address::from_string("0.0.0.0"), TEST_PORT);
        accept_ = new tcp::acceptor(ios_, addr, true);

        Accept();
        boost::thread thr([&] {
			while (is_work_) {
				try {
					ios_.run();
				}
				catch (...) {
					return;
				}
			}
        });

        work_thread_.swap(thr);
    }

    ~TestServer()
    {
        is_work_ = false;
        error_code ignore_ec;
        accept_->close(ignore_ec);
#ifdef _WIN32
        work_thread_.interrupt();
#else
        exit(0);
#endif
    }

    void Accept()
    {
        std::shared_ptr<tcp::socket> s(new tcp::socket(ios_));
        accept_->async_accept(*s, [=](error_code ec) {
            if (!is_work_) return;

            Accept();
//            fprintf(stderr, "connected %d\n", ++count_);

            if (!ec)
                Read(s, boost::shared_array<char>());
        });
    }

    void Read(std::shared_ptr<tcp::socket> s, boost::shared_array<char> sbuf)
    {
        if (!sbuf) sbuf.reset(new char[1024]);
        s->async_read_some(buffer(&sbuf[0], 1024), [=](error_code ec, uint32_t bytes) {
            this->OnRead(ec, bytes, s, sbuf);
        });
    }

    void OnRead(error_code ec, uint32_t bytes, std::shared_ptr<tcp::socket> s, boost::shared_array<char> sbuf)
    {
        if (ec) {
            Close(s);
            return;
        }

        boost::shared_array<char> write_buf(new char[bytes]);
        memcpy(&write_buf[0], &sbuf[0], bytes);
        Write(s, write_buf, 0, bytes, sbuf);
    }

    void Write(std::shared_ptr<tcp::socket> s, boost::shared_array<char> write_buf, int pos, int size, boost::shared_array<char> sbuf)
    {
        s->async_write_some(buffer(&write_buf[pos], size - pos), [=](error_code ec, uint32_t bytes) {
            if (ec) {
                Close(s);
                return;
            }

            if ((int)bytes < size - pos) {
                Write(s, write_buf, pos + bytes, size, sbuf);
            } else {
                Read(s, sbuf);
            }
        });
    }

    void Close(std::shared_ptr<tcp::socket> s)
    {
        error_code ignore_ec;
        s->close(ignore_ec);
//        if (!ignore_ec)
//            fprintf(stderr, "disconnected %d\n", --count_);
    }
};
static TestServer s_server;
