#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <netdb.h>
#include <arpa/inet.h>
using namespace std;
using namespace co;

void test_gethostbyname1(int index, int &yield_c)
{
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
}

void test_gethostbyname2()
{
    hostent* h = gethostbyname("abcdefghijklmnopqrstuvwxyz123");
    EXPECT_FALSE(!!h);
    EXPECT_EQ(h_errno, HOST_NOT_FOUND);
}

void test_gethostbyname3()
{
    hostent* h = gethostbyname("");
    EXPECT_FALSE(!!h);
    EXPECT_EQ(h_errno, NO_RECOVERY);
}

void test_gethostbyname_r1(int index, int &yield_c)
{
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
}
void test_gethostbyname_r2()
{
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("abcdefghijklmnopqrstuvwxyz123", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, -1);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, HOST_NOT_FOUND);
}
void test_gethostbyname_r3()
{
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, -1);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, NO_RECOVERY);
}
void test_gethostbyname_r4()
{
    char buf[8];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("www.baidu.com", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, -1);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, ENOEXEC);
}


TEST(testDns, testDns)
{
#if WITH_CARES
    int yield_c = 0;
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

    yield_c = 0;
    for (int i = 0; i < 10; ++i)
    {
        go [i, &yield_c]{ 
            test_gethostbyname_r1(i, yield_c); 
        };
    }
    go test_gethostbyname_r2;
    go test_gethostbyname_r3;
    go test_gethostbyname_r4;
    co_sched.RunUntilNoTask();
    EXPECT_TRUE(yield_c > 0);
    printf("yield count=%d\n", yield_c);
#endif
}
