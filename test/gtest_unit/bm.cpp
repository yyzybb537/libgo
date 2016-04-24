#include <iostream>
#include <gtest/gtest.h>
#include "coroutine.h"
#include <chrono>
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

using ::testing::TestWithParam;
using ::testing::Values;

struct stdtimer
{
    explicit stdtimer(int count = 0, std::string name = "")
        : except_duration_(0)
    {
        count_ = count;
        name_ = name;
        cout << "-------------- Start " << name << " --------------" << endl;
        DebugPrint(co::dbg_all, "Start %s", name.c_str());
        t_ = chrono::system_clock::now();
    }
    ~stdtimer() {
        if (name_.empty() || !count_) return ;
        float c = chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - t_).count() - except_duration_.count();
        cout << name_ << " run " << count_ << " times, cost " << (c / 1000) << " ms" << endl;
        cout << "per second op times: " << (size_t)((double)1000000  * count_ / c) << endl;
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

TEST_P(Times, testCo)
{
//    g_Scheduler.GetOptions().debug = dbg_scheduler | dbg_switch; 
//    g_Scheduler.GetOptions().debug_output = fopen("log", "w");
    g_Scheduler.GetOptions().stack_size = 4096;

    int tcs[] = {tc_ / 10, tc_, tc_ * 10};

    for (auto tc : tcs)
    {
        go [&] {
            for (int i = 1; i < tc; ++i)
                co_yield;
        };
        stdtimer st(tc, "Switch 1 coroutine");
//        while (!g_Scheduler.IsEmpty())
        for (int i = 0; i < tc; ++i)
            g_Scheduler.Run(co::Scheduler::erf_do_coroutines);
    }

    // 1 thread test
    {
        stdtimer st(tc_ / 2, "Create coroutine half(1/2)");
        for (int i = 0; i < tc_ / 2; ++i) {
            go []{ co_yield; co_yield; };
        }
    }
    {
        stdtimer st(tc_ / 2, "Create coroutine half(2/2)");
        for (int i = 0; i < tc_ / 2; ++i) {
            go []{ co_yield; co_yield; };
        }
    }
    {
        stdtimer st(tc_, "Collect & Switch coroutine");
        g_Scheduler.Run();
    }
    {
        stdtimer st(tc_, "Switch coroutine");
        g_Scheduler.Run(co::Scheduler::erf_do_coroutines);
    }

    {
        stdtimer st(tc_, "Switch & Delete coroutine");
        g_Scheduler.RunUntilNoTask();
        g_Scheduler.RunUntilNoTask();
    }
    g_Scheduler.RunUntilNoTask();

    {
        for (int i = 0; i < 1000; ++i)
            go [&] {
                for (int i = 1; i < tc_ / 1000; ++i)
                    co_yield;
            };
        stdtimer st(tc_, "Switch 1000 coroutine");
        g_Scheduler.RunUntilNoTask();
    }


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
        for (int i = 0; i < 4; ++i)
            tg.create_thread( []{ g_Scheduler.RunUntilNoTask(); } );
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
        for (int i = 0; i < 4; ++i)
            tg.create_thread( []{ g_Scheduler.RunUntilNoTask(); } );
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
