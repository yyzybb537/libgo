#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include "coroutine.h"
#include <vector>
#include <list>
#include <atomic>
#include "gtest_exit.h"
using namespace co;

static std::atomic<int> g_value{0};

void inc_foo()
{
    co_yield;
    ++g_value;
}

void throw_foo()
{
    throw 5;
}

void yield_throw_foo()
{
    co_yield;
    throw 5;
}

typedef void fn_t();
struct ExceptionFuncTest : public ::testing::TestWithParam<fn_t*>
{
    fn_t* fn_;

    void SetUp() { fn_ = GetParam(); }
};

TEST_P(ExceptionFuncTest, Delay)
{
    g_Scheduler.GetOptions().exception_handle = eCoExHandle::delay_rethrow;
    go fn_;
    while (!g_Scheduler.IsEmpty())
    {
        try {
            g_Scheduler.Run();
        } catch (int v) {
            EXPECT_EQ(v, 5);
        } catch (...) {
            EXPECT_TRUE(false);
        }
    }
    EXPECT_TRUE(g_Scheduler.IsEmpty());
}

TEST_P(ExceptionFuncTest, ExceptionSafe)
{
    g_Scheduler.GetOptions().exception_handle = eCoExHandle::delay_rethrow;
    g_Scheduler.GetOptions().stack_size = 8 * 1024;
    for (int i = 0; i < 100; ++i)
        go fn_;

    int c = 0;
    while (!g_Scheduler.IsEmpty())
    {
        try {
            g_Scheduler.Run();
        } catch (int v) {
            ++c;
            EXPECT_EQ(v, 5);
		} catch (std::system_error&) {
			printf("create coroutine failed.");
			EXPECT_TRUE(false);
        } catch (...) {
            EXPECT_TRUE(false);
        }
    }
    EXPECT_TRUE(g_Scheduler.IsEmpty());
    EXPECT_EQ(c, 100);
    g_Scheduler.GetOptions().stack_size = 128 * 1024;
}

TEST_P(ExceptionFuncTest, ExceptionSafe2)
{
    g_Scheduler.GetOptions().exception_handle = eCoExHandle::delay_rethrow;
    g_Scheduler.GetOptions().stack_size = 8 * 1024;
    for (int i = 0; i < 100; ++i) {
        go fn_;
        go inc_foo;
    }

    int c = 0;
    while (!g_Scheduler.IsEmpty())
    {
        try {
            g_Scheduler.Run();
        } catch (int v) {
            ++c;
            EXPECT_EQ(v, 5);
        } catch (...) {
            EXPECT_TRUE(false);
        }
    }
    EXPECT_TRUE(g_Scheduler.IsEmpty());
    EXPECT_EQ(c, 100);
    EXPECT_EQ(g_value, 100);
    g_Scheduler.GetOptions().stack_size = 128 * 1024;
    g_value = 0;
}

TEST_P(ExceptionFuncTest, ExceptionSafe3)
{
    g_Scheduler.GetOptions().exception_handle = eCoExHandle::delay_rethrow;
    g_Scheduler.GetOptions().stack_size = 16 * 1024;
    for (int i = 0; i < 100; ++i) {
        go fn_;
        go inc_foo;
    }

    std::atomic_int c{0};
    boost::thread_group tg;
    for (int i = 0; i < 8; ++i)
        tg.create_thread( [&c]{
                while (!g_Scheduler.IsEmpty())
                {
                    try {
                        g_Scheduler.Run();
                    } catch (int v) {
                        ++c;
                        EXPECT_EQ(v, 5);
                    } catch (...) {
                        EXPECT_TRUE(false);
                    }
                }
            });
    tg.join_all();
    EXPECT_TRUE(g_Scheduler.IsEmpty());
    EXPECT_EQ(c, 100);
    EXPECT_EQ(g_value, 100);
    g_Scheduler.GetOptions().stack_size = 128 * 1024;
    g_value = 0;
}

using ::testing::Values;
INSTANTIATE_TEST_CASE_P(
        CoException,
        ExceptionFuncTest,
        Values(&throw_foo, &yield_throw_foo));
