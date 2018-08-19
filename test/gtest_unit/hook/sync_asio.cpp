#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
#include "test_server.h"
#include "coroutine.h"
#include <chrono>
#include <boost/asio.hpp>
#include <time.h>
#include "../gtest_exit.h"
using namespace co;
using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

TEST(SyncAsio, sync_connect)
{
    io_service ios;
	
    tcp::endpoint addr(address::from_string("127.0.0.1"), TEST_PORT);
    tcp::socket s(ios);
    error_code ec;
    s.connect(addr, ec);
    EXPECT_TRUE(!ec);
}
