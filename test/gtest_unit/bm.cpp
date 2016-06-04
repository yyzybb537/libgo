#include <iostream>
#include <gtest/gtest.h>
#include <chrono>
#include <boost/thread.hpp>
#include <boost/coroutine/all.hpp>
#include "gtest_exit.h"
#define private public
#include "coroutine.h"
using namespace std;
using namespace co;

using ::testing::TestWithParam;
using ::testing::Values;

struct stdtimer
{
    explicit stdtimer(int count = 0, std::string name = "")
        : except_duration_(0)
    {
        if (!count) return ;
        count_ = count;
        name_ = name;
        cout << "-------------- Start " << name << " --------------" << endl;
        DebugPrint(co::dbg_all, "Start %s", name.c_str());
        t_ = chrono::system_clock::now();
    }
    ~stdtimer() {
        if (name_.empty() || !count_) return ;
        auto now = chrono::system_clock::now();
        float c = chrono::duration_cast<chrono::microseconds>(now - t_).count() - except_duration_.count();
        double ns = chrono::duration_cast<chrono::nanoseconds>(now - t_).count() - except_duration_.count() * 1000 * 1000;
        cout << name_ << " run " << count_ << " times, cost " << (c / 1000) << " ms" << endl;
        cout << "per second op times: " << (size_t)((double)1000000  * count_ / c) << endl;
        cout << "per op cost: " << int64_t(ns / count_) << " ns" << endl;
        cout << "-----------------------------------------------------------" << endl;
    }

    chrono::microseconds expired()
    {
        return chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - t_);
    }

    void except(chrono::microseconds t) {
        except_duration_ = t;
    }

    chrono::system_clock::time_point t_;
    std::string name_;
    int count_;
    chrono::microseconds except_duration_;
};

struct Times : public TestWithParam<int>
{
    int tc_;
    void SetUp() { tc_ = GetParam(); }
};

void foo (int tc_, bool dump)
{
//    g_Scheduler.GetOptions().stack_size = 8192;

    int tcs[] = {tc_ * 10, tc_, tc_ / 10};

    for (auto tc : tcs)
    {
        go [&] {
            for (int i = 1; i < tc; ++i)
                co_yield;
        };
        if (dump) {
            stdtimer st(tc, "Switch 1 coroutine");
            while (!g_Scheduler.IsEmpty())
                g_Scheduler.Run(co::Scheduler::erf_do_coroutines);
        } else
            g_Scheduler.RunUntilNoTask();
    }
}

void ones_loop(int tc_)
{
    co::Context **pp_ctx = new co::Context*;
    *pp_ctx = new co::Context(8192, [=]{
            for (int i = 1; i < tc_; ++i)
                (*pp_ctx)->SwapOut();
            });

    ContextScopedGuard scg;
    (void)scg;

    stdtimer st(tc_, "Switch 1 Contxt");
    for (int i = 1; i < tc_; ++i)
        (*pp_ctx)->SwapIn();
}

TEST_P(Times, context)
{
    foo(tc_, false);
    foo(tc_, true);

//    for (int i = 0; i < 10; ++i)
        ones_loop(tc_);
}

struct CtxWrap : public co::TSQueueHook
{
    co::Context** pp_ctx;
};

uint32_t process_proc(TSQueue<CtxWrap> & ts_list)
{
    ContextScopedGuard scg;
    (void)scg;

    uint32_t c = 0;
    CtxWrap *pos = (CtxWrap*)ts_list.head_->next;
    while (pos) {
        ++c;
        CtxWrap* wrap = pos;
        pos = (CtxWrap*)pos->next;
        (*wrap->pp_ctx)->SwapIn();
    }
    return c;
}

TEST_P(Times, context_ts)
{
    int rv = 0;
    for (int i = 0; i < 100; ++i)
    {
        CtxWrap* wrap = new CtxWrap;
        co::Context** &pp_ctx = wrap->pp_ctx;
        pp_ctx = new co::Context*;
        *pp_ctx = new co::Context(1024 * 1024, [&]{
                    for (int i = 1; i < tc_; ++i) {
                        ++rv;
                        (*pp_ctx)->SwapOut();
                    }
                    ++rv;
                });

        TSQueue<CtxWrap> ts_list;
        ts_list.push(wrap);

        stdtimer st(i < 95 ? 0 :tc_, "Switch 1 Contxt in TSQueue");
        for (int i = 1; i < tc_; ++i) {
            uint32_t c = process_proc(ts_list);
            (void)c;
        }
    }
    cout << "rv: " << rv << endl;
}

typedef ::boost::coroutines::symmetric_coroutine<void>::call_type co_t;
typedef ::boost::coroutines::symmetric_coroutine<void>::yield_type yd_t;
using namespace std::chrono;

yd_t *pyd = nullptr;
int c = 100000;
int rv = 0;
void foo()
{
    for (int i = 1; i < c; ++i)
    {
        ++rv;
        (*pyd)();
    }
    ++rv;
}

void test_boost()
{
    co_t **pp_co = new co_t*;
    *pp_co = new co_t([&](yd_t& yd) {
            pyd = &yd;
            foo();
            }, boost::coroutines::attributes(1024 * 1024));

    stdtimer st(100000, "boost.coroutines switch");
    auto s = system_clock::now();
    for (int i = 0; i < c; ++i)
        (**pp_co)();
    auto e = system_clock::now();
    cout << duration_cast<microseconds>(e-s).count() << " us" << endl;
    cout << "rv:" << rv << endl;
}

TEST_P(Times, boost)
{
    test_boost();
}

TEST_P(Times, proc)
{
    g_Scheduler.Run();
    auto& info = CoDebugger::getInstance().GetLocalInfo();

    int rv = 0;
    for (int i = 0; i < 100; ++i)
    {
        int tc = tc_;
        go [&] {
            for (int i = 1; i < tc; ++i) {
                ++rv;
                co_yield;
            }
            ++rv;
        };
        stdtimer st(i < 95 ? 0 :tc, "Switch 1 coroutine in Processer");
        for (int i = 0; i < tc; ++i)
        {
            uint32_t d;
            info.proc->Run(d);
        }
        --g_Scheduler.task_count_;
    }
    cout << "rv:" << rv << endl;
}

TEST_P(Times, testCo)
{
//    g_Scheduler.GetOptions().debug = dbg_scheduler;
//    g_Scheduler.GetOptions().debug_output = fopen("log", "w");
    g_Scheduler.GetOptions().stack_size = 8192;

//    int tcs[] = {tc_ / 10, tc_, tc_ * 10};
    int tcs[] = {tc_};
    int coro_c[] = {1, 10, 100};

    for (auto co_c : coro_c)
    {
        for (auto tc : tcs)
        {
            for (int i = 0; i < co_c; ++i)
                go [&] {
                    for (int i = 1; i < tc; ++i)
                        co_yield;
                };
            stdtimer st(tc * co_c, "Switch " + std::to_string(co_c) + " coroutine alone");
            while (g_Scheduler.Run(co::Scheduler::erf_do_coroutines));
        }

        g_Scheduler.RunUntilNoTask();

        for (auto tc : tcs)
        {
            for (int i = 0; i < co_c; ++i)
                go [&] {
                    for (int i = 1; i < tc; ++i)
                        co_yield;
                };
            stdtimer st(tc * co_c, "Switch " + std::to_string(co_c) + " coroutine flags-all");
            g_Scheduler.RunUntilNoTask();
        }

        g_Scheduler.RunUntilNoTask();
    }


    for (auto tc : tcs)
    {
        stdtimer st(tc, "Switch 0 coroutine");
        for (int i = 0; i < tc; ++i)
            g_Scheduler.Run(co::Scheduler::erf_do_coroutines);
    }

    // 1 thread test
    int rv = 0;
    {
        stdtimer st(tc_ / 2, "Create coroutine half(1/2)");
        for (int i = 0; i < tc_ / 2; ++i) {
            go [&]{ ++rv; co_yield; ++rv; co_yield; ++rv; };
        }
    }
    {
        stdtimer st(tc_ / 2, "Create coroutine half(2/2)");
        for (int i = 0; i < tc_ / 2; ++i) {
            go [&]{ ++rv; co_yield; ++rv; co_yield; ++rv; };
        }
    }
    {
        stdtimer st(tc_, "Collect & Switch coroutine");
        g_Scheduler.Run();
    }
    cout << "rv:" << rv << endl;
    {
        stdtimer st(tc_, "Switch coroutine");
        g_Scheduler.Run();
    }
    cout << "rv:" << rv << endl;

    {
        stdtimer st(tc_, "Switch & Delete coroutine");
        g_Scheduler.RunUntilNoTask();
    }
    cout << "rv:" << rv << endl;
    g_Scheduler.GetOptions().debug = 0;

    {
        for (int i = 0; i < 1000; ++i)
            go [&] {
                for (int i = 1; i < tc_ / 1000; ++i)
                    co_yield;
            };
        stdtimer st(tc_, "Switch 1000 coroutine");
        g_Scheduler.RunUntilNoTask();
    }


//    g_Scheduler.GetOptions().debug = dbg_scheduler;
//    g_Scheduler.GetOptions().debug_output = fopen("log", "w");

    // 4 threads test
    {
        stdtimer st(tc_, "Create coroutine - 2");
        for (int i = 0; i < tc_; ++i) {
            go []{ co_yield; };
        }
    }
    {
        g_Scheduler.Run();

        stdtimer st(tc_, "4 threads Switch & Delete coroutine");
        boost::thread_group tg;
        for (int i = 0; i < 3; ++i)
            tg.create_thread( []{ g_Scheduler.RunUntilNoTask(); } );
        g_Scheduler.RunUntilNoTask();
        tg.join_all();
    }
    g_Scheduler.RunUntilNoTask();

    {
        stdtimer st(tc_, "4 threads Create coroutine");
        boost::thread_group tg;
        for (int i = 0; i < 4; ++i)
            tg.create_thread( [=]{
                        for (int i = 0; i < tc_ / 4; ++i) {
                            go []{};
                        }
                    } );
        tg.join_all();
    }
    {
        stdtimer st(tc_, "4 threads Collect & Switch & Delete coroutine");
        boost::thread_group tg;
        for (int i = 0; i < 3; ++i)
            tg.create_thread( []{ g_Scheduler.RunUntilNoTask(); } );
        g_Scheduler.RunUntilNoTask();
        tg.join_all();
    }
    g_Scheduler.RunUntilNoTask();
}

TEST_P(Times, testChan)
{
//    co_chan<int> chan;
    co_chan<int> chan(tc_);
    go [=] {
        for (int i = 0; i < tc_; ++i) {
            chan << i;
        }
    };

    go [=] {
        int c;
        for (int i = 0; i < tc_; ++i)
            chan >> c;
    };

    {
        stdtimer st(tc_, "Channel");
        g_Scheduler.RunUntilNoTask();
    }
    g_Scheduler.RunUntilNoTask();
}


TEST_P(Times, testTimer)
{
    {
        stdtimer st(tc_, "Create Timer");
        for (int i = 0; i < tc_; ++i)
#ifndef _WIN32
            co_timer_add(std::chrono::nanoseconds(i),
#else
			co_timer_add(std::chrono::microseconds(i / 1000),
#endif
                [=]{
                    if ((i + 1) % 10000 == 0) {
                        //printf("run %d\n", i);
                    }
                });
    }

    {
        std::vector<co_timer_id> id_list;
        id_list.reserve(tc_);
        for (int i = 0; i < tc_; ++i)
#ifndef _WIN32
            id_list.push_back(co_timer_add(std::chrono::nanoseconds(i), []{}));
#else
			id_list.push_back(co_timer_add(std::chrono::microseconds(i / 1000), []{}));
#endif

        stdtimer st(tc_, "Delete Timer");
        for (int i = 0; i < tc_; ++i)
            co_timer_cancel(id_list[i]);
    }

//    g_Scheduler.GetOptions().debug = co::dbg_timer;
    {
        bool *flag = new bool(true);
        auto id = co_timer_add(std::chrono::milliseconds(1), [=]{ *flag = false; });

        stdtimer st(tc_, "Process Timer");
        while (*flag)
            g_Scheduler.Run();
    }
}

#ifdef SMALL_TEST
INSTANTIATE_TEST_CASE_P(
        BmTest,
        Times,
        Values(10000));
#else
INSTANTIATE_TEST_CASE_P(
        BmTest,
        Times,
        Values(100000));
//        Values(1000000, 3000000, 10000000));
#endif
