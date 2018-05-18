#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <netdb.h>
#include <resolv.h>
#include <arpa/inet.h>
using namespace std;
using namespace co;

void test_gethostbyname1(int index, int &yield_c)
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    hostent* h = gethostbyname("www.baidu.com");
    EXPECT_TRUE(!!h);
    if (h) {
        int i = 0;
        for (char **p = h->h_addr_list; *p; ++p, ++i)
        {
            char buf[128];
            printf("[%d] addr[%d]: %s\n", index, i, inet_ntop(h->h_addrtype, *p, buf, sizeof(buf)));
        }
    } else {
        printf("[%d]returns nullptr\n", index);
    }
    yield_c += co_sched.GetCurrentTaskYieldCount();
    printf("{%d} gethostbyname[%d] done\n", co_sched.GetCurrentTaskID(), idx++);
}

void test_gethostbyname2()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname2[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    hostent* h = gethostbyname("abcdefghijklmnopqrstuvwxyz123");
    EXPECT_FALSE(!!h);
    EXPECT_EQ(h_errno, HOST_NOT_FOUND);
    printf("{%d} gethostbyname2[%d] done\n", co_sched.GetCurrentTaskID(), idx);
}

void test_gethostbyname3()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname3[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    hostent* h = gethostbyname("");
    EXPECT_FALSE(!!h);
    EXPECT_EQ(h_errno, NO_DATA);
    printf("{%d} gethostbyname3[%d] done\n", co_sched.GetCurrentTaskID(), idx);
}

void test_gethostbyname_r1(int index, int &yield_c)
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r1[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("www.baidu.com", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, 0);
    EXPECT_TRUE(!!h);
    if (h) {
        int i = 0;
        for (char **p = h->h_addr_list; *p; ++p, ++i)
        {
            char buf[128];
            printf("[%d] addr[%d]: %s\n", index, i, inet_ntop(h->h_addrtype, *p, buf, sizeof(buf)));
        }
    } else {
        printf("[%d]returns nullptr\n", index);
    }
    yield_c += co_sched.GetCurrentTaskYieldCount();
    printf("{%d} gethostbyname_r1[%d] done\n", co_sched.GetCurrentTaskID(), idx);
}
void test_gethostbyname_r2()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r2[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("abcdefghijklmnopqrstuvwxyz123", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, 0);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, HOST_NOT_FOUND);
    printf("{%d} gethostbyname_r2[%d] done\n", co_sched.GetCurrentTaskID(), idx);
}
void test_gethostbyname_r3()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r3[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, 0);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, NO_DATA);
    printf("{%d} gethostbyname_r3[%d] done\n", co_sched.GetCurrentTaskID(), idx);
}
void test_gethostbyname_r4()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r4[%d] begin\n", co_sched.GetCurrentTaskID(), idx);
    char buf[8];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("www.baidu.com", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, ERANGE);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, NETDB_INTERNAL);
    printf("{%d} gethostbyname_r4[%d] done\n", co_sched.GetCurrentTaskID(), idx);
}

void printDebug() {
    printf("----------------- ---------------- -------------------\n");
    printf("DebugInfo:%s\n", co::CoDebugger::getInstance().GetAllInfo().c_str());
}

TEST(testDns, testDns)
{

    int yield_c = 0;

//    co_sched.GetOptions().debug = co::dbg_hook;

//    printf("call poll\n");
//    poll(nullptr, 0, 0);
//    printf("call __res_state\n");
//    auto rs = __res_state();
//    go []{
//        auto rs = __res_state();
//    };
//    co_sched.RunUntilNoTask();
//    return ;

    for (int i = 0; i < 10; ++i)
    {
        go [i, &yield_c]{ 
            test_gethostbyname1(i, yield_c); 
        };
    }

    go test_gethostbyname2;
    go test_gethostbyname3;

    co_sched.RunUntilNoTask();
    EXPECT_TRUE(yield_c > 0);
    printf("yield count=%d\n", yield_c);
//    printDebug();

    yield_c = 0;
    for (int i = 0; i < 10; ++i)
    {
        go [i, &yield_c]{ 
            test_gethostbyname_r1(i, yield_c); 
        };
    }
    co_sched.RunUntilNoTask();
    EXPECT_TRUE(yield_c > 0);
    printf("yield count=%d\n", yield_c);
//    printDebug();

    go test_gethostbyname_r2;
    go test_gethostbyname_r3;
    go test_gethostbyname_r4;
    co_sched.RunUntilNoTask();
    EXPECT_TRUE(yield_c > 0);
    printf("yield count=%d\n", yield_c);
//    printDebug();
}
