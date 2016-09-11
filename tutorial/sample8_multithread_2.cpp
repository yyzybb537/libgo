/************************************************
 * libgo sample1
 * 模拟多个线程同时输入，一个协程接收并处理的情况
*************************************************/
#include "coroutine.h"
#include "win_exit.h"
#include <stdio.h>
#include <thread>
#include <future>
#include <iostream>
#include <vector>
#include <ppltasks.h>
#include <agents.h>

int main()
{
    class Task {
    public:
      Task() {
        std::thread([]{
          std::cout << "co thread: " << std::this_thread::get_id() << std::endl;
          co_sched.RunLoop();
        }).detach();
      }
      void operator()() {
        go [] {
          std::cout << "co thread: " << std::this_thread::get_id() << std::endl;
          //std::this_thread::sleep_for(std::chrono::seconds(1));
        };
      }
    };

    Task task;       

    auto count = 5;
    std::vector<std::unique_ptr<concurrency::timer<int> > > timers;
    std::vector<std::unique_ptr<concurrency::call<int> > > callers;
    timers.resize(count); callers.resize(count);
    for (auto i = 0; i < count; ++i) {
      timers[i].reset(new concurrency::timer<int>(1000, 0, nullptr, false));
      callers[i].reset(new concurrency::call<int>([&task, &timers, &callers, i](int) {
        std::cout << "timer thread: " << std::this_thread::get_id() << std::endl;
        task();
        timers[i].reset(new concurrency::timer<int>(1000, 0, nullptr, false));
        timers[i]->link_target(callers[i].get());
        timers[i]->start();
      }));
      timers[i]->link_target(callers[i].get());
      timers[i]->start();
    }
    
    getchar();

    return 0;
}

