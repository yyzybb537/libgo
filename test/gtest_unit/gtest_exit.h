#pragma once
#include "../../libgo/libgo.h"
#include <thread>
#include "gtest/gtest.h"
# include <unistd.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <poll.h>

#define DEBUG_CHECK_POINT

#ifndef TEST_MIN_THREAD
#define TEST_MIN_THREAD 4
#endif

#ifndef TEST_MAX_THREAD
#define TEST_MAX_THREAD 4
#endif

struct startScheduler {
    startScheduler(co::Scheduler & scheduler) {
        pThread = new std::thread([&]{
                scheduler.Start(TEST_MIN_THREAD, TEST_MAX_THREAD);
            });
    }
    std::thread *pThread;
};

startScheduler __startScheduler(g_Scheduler);

inline void __WaitUntilNoTask(co::Scheduler & scheduler, int line, std::size_t val = 0) {
    int i = 0;
    while (scheduler.TaskCount() > val) {
        usleep(1000);
        if (++i == 9000) {
            printf("LINE: %d, TaskCount: %d\n", line, (int)g_Scheduler.TaskCount());
        }
    }
}

#define WaitUntilNoTask() __WaitUntilNoTask(g_Scheduler, __LINE__)
#define WaitUntilNoTaskN(n) __WaitUntilNoTask(g_Scheduler, __LINE__, n)

#define WaitUntilNoTaskS(scheduler) __WaitUntilNoTask(scheduler, __LINE__)

inline void DumpTaskCount() {
    printf("TaskCount: %d\n", (int)g_Scheduler.TaskCount());
}

template <typename Clock>
struct GTimerT {
    GTimerT() : tp_(Clock::now()) {}

    void reset() {
        tp_ = Clock::now();
    }

    typename Clock::duration duration() {
        return Clock::now() - tp_;
    }

    int ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - tp_).count();
    }

private:
    typename Clock::time_point tp_;
};
typedef GTimerT<co::FastSteadyClock> GTimer;

struct CheckPoint
{
    static CheckPoint & getInstance() {
        static CheckPoint obj;
        return obj;
    }

    CheckPoint() {
        std::thread([this]{
                    for (;;) {
                        sleep(1);
                        this->CheckBlock();
                    }
                }).detach();
    }

    void Set(const char* file, int line) {
        tp_ = co::FastSteadyClock::now();
        file_ = file;
        line_ = line;
    }

    void CheckBlock() {
        auto dur = co::FastSteadyClock::now() - tp_;
        if (tp_.time_since_epoch().count() > 0 && dur > std::chrono::seconds(5)) {
            printf("check block point: file(%s), line(%d)\n", file_, line_);
            tp_ = co::FastSteadyClock::time_point();
        }
    }

    const char* file_;
    int line_;
    co::FastSteadyClock::time_point tp_;
};

#define DEFAULT_DEVIATION 50
#define TIMER_CHECK(t, val, deviation) \
        do { \
            auto c = t.ms(); \
            EXPECT_GT(c, val - 2); \
            EXPECT_LT(c, val + deviation); \
        } while (0)

#if defined(DEBUG_CHECK_POINT)
# define CHECK_POINT() do {\
        CheckPoint::getInstance().Set(__FILE__, __LINE__); \
    } while(0)
#else
# define CHECK_POINT() do {} while(0)
#endif

// hook gtest macros
#undef EXPECT_EQ
#define EXPECT_EQ(expected, actual) \
  CHECK_POINT(); \
  EXPECT_PRED_FORMAT2(::testing::internal:: \
                      EqHelper<GTEST_IS_NULL_LITERAL_(expected)>::Compare, \
                      expected, actual)

#undef EXPECT_NE
#define EXPECT_NE(expected, actual) \
  CHECK_POINT(); \
  EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperNE, expected, actual)

#undef EXPECT_LE
#define EXPECT_LE(val1, val2) \
  CHECK_POINT(); \
  EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperLE, val1, val2)

#undef EXPECT_LT
#define EXPECT_LT(val1, val2) \
  CHECK_POINT(); \
  EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperLT, val1, val2)

#undef EXPECT_GE
#define EXPECT_GE(val1, val2) \
  CHECK_POINT(); \
  EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperGE, val1, val2)

#undef EXPECT_GT
#define EXPECT_GT(val1, val2) \
  CHECK_POINT(); \
  EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperGT, val1, val2)

#undef EXPECT_TRUE
#define EXPECT_TRUE(condition) \
  CHECK_POINT(); \
  GTEST_TEST_BOOLEAN_(condition, #condition, false, true, \
                      GTEST_NONFATAL_FAILURE_)

#undef EXPECT_FALSE
#define EXPECT_FALSE(condition) \
  CHECK_POINT(); \
  GTEST_TEST_BOOLEAN_(!(condition), #condition, true, false, \
                      GTEST_NONFATAL_FAILURE_)

#undef ASSERT_TRUE
#define ASSERT_TRUE(condition) \
  CHECK_POINT(); \
  GTEST_TEST_BOOLEAN_(condition, #condition, false, true, \
                      GTEST_FATAL_FAILURE_)

#undef ASSERT_FALSE
#define ASSERT_FALSE(condition) \
  CHECK_POINT(); \
  GTEST_TEST_BOOLEAN_(!(condition), #condition, true, false, \
                      GTEST_FATAL_FAILURE_)

#if defined(LIBGO_SYS_Windows)
struct __autoInitWSA {
    __autoInitWSA() {
        WSAData wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
};
__autoInitWSA g___autoInitWSA;
#endif

int tcpSocketPair(int, int, int, int fds[2])
{
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int res = bind(listenSock, (sockaddr*)&addr, sizeof(addr));
    if (res < 0) {
        close(listenSock);
        return -1;
    }
    listen(listenSock, 5);

    socklen_t addrLen = sizeof(addr);
    res = getsockname(listenSock, (sockaddr*)&addr, &addrLen);
    if (res < 0) {
        close(listenSock);
        return -1;
    }

    int clientSock = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSock < 0) {
        close(listenSock);
        return -1;
    }

    std::thread([=]{
                connect(clientSock, (sockaddr*)&addr, addrLen);
            }).detach();

    struct pollfd pfd = {};
    pfd.fd = listenSock;
    pfd.events = POLLIN;
    res = poll(&pfd, 1, 1000);
    if (res <= 0) {
        close(listenSock);
        close(clientSock);
        return -1;
    }

    int newSock = accept(listenSock, (sockaddr*)&addr, &addrLen);
    if (newSock <= 0) {
        close(listenSock);
        close(clientSock);
        return -1;
    }

    int bufSize = 32*1024;
    setsockopt(newSock,SOL_SOCKET,SO_RCVBUF,(const char*)&bufSize,sizeof(int));
    setsockopt(newSock,SOL_SOCKET,SO_SNDBUF,(const char*)&bufSize,sizeof(int));
    setsockopt(clientSock,SOL_SOCKET,SO_RCVBUF,(const char*)&bufSize,sizeof(int));
    setsockopt(clientSock,SOL_SOCKET,SO_SNDBUF,(const char*)&bufSize,sizeof(int));

    fds[0] = clientSock;
    fds[1] = newSock;
    //close(listenSock);
    return 0;
}

#ifdef _WIN32
#include <stdlib.h>
struct exit_pause {
	~exit_pause()
	{
		system("pause");
	}
} g_exit;
#endif
