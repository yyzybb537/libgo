#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <netdb.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <poll.h>
#define TEST_MIN_THREAD 1
#define TEST_MAX_THREAD 1
#include "../gtest_exit.h"
using namespace std;
using namespace co;

typedef struct hostent* (*getXXbyYY)();
typedef int (*getXXbyYY_r)(struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop);
getXXbyYY g_getXXbyYY = NULL;
getXXbyYY_r g_getXXbyYY_r = NULL;
const char* fname = "fname";
const char* test_host = "www.huya.com";

struct Cache {
    const void *addr;
    socklen_t len; 
    int type;

    Cache() : addr(nullptr) {}
//    ~Cache() { if (addr) free(addr); }
    operator bool() const { return addr != nullptr; }
};
Cache g_cache;

struct hostent* getXXbyYY_1() {
    return gethostbyname(test_host);
}

struct hostent* getXXbyYY_2() {
    return gethostbyname2(test_host, AF_INET);
}

struct hostent* getXXbyYY_3() {
    return gethostbyaddr(g_cache.addr, g_cache.len, g_cache.type);
}

int getXXbyYY_r_1(struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    return gethostbyname_r(test_host, ret, buf, buflen, result, h_errnop);
}

int getXXbyYY_r_2(struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    return gethostbyname2_r(test_host, AF_INET, ret, buf, buflen, result, h_errnop);
}

int getXXbyYY_r_3(struct hostent *ret, char *buf, size_t buflen,
        struct hostent **result, int *h_errnop)
{
    return gethostbyaddr_r(g_cache.addr, g_cache.len, g_cache.type,
            ret, buf, buflen, result, h_errnop);
}

void test_getXXbyYY(int index, int &yield_c)
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} %s[%d] begin\n", (int)co_sched.GetCurrentTaskID(), fname, idx);
    hostent* h = g_getXXbyYY();
    EXPECT_TRUE(!!h);
    if (h) {
        int i = 0;
        for (char **p = h->h_addr_list; *p; ++p, ++i)
        {
            char buf[128];
            printf("[%d] addr[%d]: %s\n", index, i, inet_ntop(h->h_addrtype, *p, buf, sizeof(buf)));

            if (!g_cache) {
                sockaddr_in *addr = new sockaddr_in;
                addr->sin_family = h->h_addrtype;
                addr->sin_port = htons(80);
                addr->sin_addr.s_addr = inet_addr(buf);

                g_cache.addr = addr;
                g_cache.len = sizeof(sockaddr_in);
                g_cache.type = h->h_addrtype;
            }
        }
    } else {
        printf("[%d]returns nullptr\n", index);
    }
    yield_c += co_sched.GetCurrentTaskYieldCount();
    printf("{%d} %s[%d] done\n", (int)co_sched.GetCurrentTaskID(), fname, idx++);
}

void test_gethostbyname2()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname2[%d] begin\n", (int)co_sched.GetCurrentTaskID(), idx);
    hostent* h = gethostbyname("abcdefghijklmnopqrstuvwxyz123");
    EXPECT_FALSE(!!h);
    EXPECT_EQ(h_errno, HOST_NOT_FOUND);
    printf("{%d} gethostbyname2[%d] done\n", (int)co_sched.GetCurrentTaskID(), idx);
}

void test_gethostbyname3()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname3[%d] begin\n", (int)co_sched.GetCurrentTaskID(), idx);
    hostent* h = gethostbyname("");
    EXPECT_FALSE(!!h);
    EXPECT_FALSE(h_errno == NETDB_SUCCESS);
    printf("{%d} gethostbyname3[%d] done\n", (int)co_sched.GetCurrentTaskID(), idx);
}

void test_getXXbyYY_r(int index, int &yield_c)
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} %s[%d] begin\n", (int)co_sched.GetCurrentTaskID(), fname, idx);
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = g_getXXbyYY_r(h, buf, sizeof(buf), &h, &err);
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
    printf("{%d} %s[%d] done\n", (int)co_sched.GetCurrentTaskID(), fname, idx);
}
void test_gethostbyname_r2()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r2[%d] begin\n", (int)co_sched.GetCurrentTaskID(), idx);
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("abcdefghijklmnopqrstuvwxyz123", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, 0);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, HOST_NOT_FOUND);
    printf("{%d} gethostbyname_r2[%d] done\n", (int)co_sched.GetCurrentTaskID(), idx);
}
void test_gethostbyname_r3()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r3[%d] begin\n", (int)co_sched.GetCurrentTaskID(), idx);
    char buf[8192];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r("", h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, 0);
    EXPECT_FALSE(!!h);
    EXPECT_FALSE(h_errno == NETDB_SUCCESS);
    printf("{%d} gethostbyname_r3[%d] done\n", (int)co_sched.GetCurrentTaskID(), idx);
}
void test_gethostbyname_r4()
{
    static int sidx = 0;
    int idx = sidx++;
    printf("{%d} gethostbyname_r4[%d] begin\n", (int)co_sched.GetCurrentTaskID(), idx);
    char buf[8];
    hostent xh;
    hostent *h = &xh;
    int err = 0;
    int res = gethostbyname_r(test_host, h, buf, sizeof(buf), &h, &err);
    EXPECT_EQ(res, ERANGE);
    EXPECT_FALSE(!!h);
    EXPECT_EQ(err, NETDB_INTERNAL);
    printf("{%d} gethostbyname_r4[%d] done\n", (int)co_sched.GetCurrentTaskID(), idx);
}

TEST(testDns, testDns)
{
//    co_opt.debug = dbg_hook;

    int yield_c = 0;

    close(-1);

    go test_gethostbyname2;
    go test_gethostbyname3;
    go test_gethostbyname_r2;
    go test_gethostbyname_r3;
    go test_gethostbyname_r4;
    WaitUntilNoTask();

    getXXbyYY funcs1[] = {getXXbyYY_1, getXXbyYY_2, getXXbyYY_3};
    const char* fnames1[] = {"gethostbyname", "gethostbyname2", "gethostbyaddr"};
    for (std::size_t idx = 0; idx < sizeof(funcs1)/sizeof(getXXbyYY); idx++) {
        g_getXXbyYY = funcs1[idx];
        fname = fnames1[idx];
        yield_c = 0;
        for (int i = 0; i < 10; ++i)
        {
            go [i, &yield_c]{ 
                test_getXXbyYY(i, yield_c); 
            };
        }
        WaitUntilNoTask();
//        EXPECT_TRUE(yield_c > 0);
        printf("yield count=%d\n", yield_c);
        //printDebug();
    }

    getXXbyYY_r funcs2[] = {getXXbyYY_r_1, getXXbyYY_r_2, getXXbyYY_r_3};
    const char* fnames2[] = {"gethostbyname_r", "gethostbyname2_r", "gethostbyaddr_r"};
    for (std::size_t idx = 0; idx < sizeof(funcs2)/sizeof(getXXbyYY_r); idx++) {
        g_getXXbyYY_r = funcs2[idx];
        fname = fnames2[idx];
        yield_c = 0;
        for (int i = 0; i < 10; ++i)
        {
            go [i, &yield_c]{ 
                test_getXXbyYY_r(i, yield_c); 
            };
        }
        WaitUntilNoTask();
//        EXPECT_TRUE(yield_c > 0);
        printf("yield count=%d\n", yield_c);
        //printDebug();
    }
}

