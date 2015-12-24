#include <boost/crc.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <iostream>
#include "coroutine.h"

void foo()
{
    static char const buf[65536] = {};
    boost::crc_32_type crc;
    for (int i = 0; i < 1000; ++i)
        crc.process_bytes(buf, sizeof(buf));
//    std::cout << crc.checksum() << std::endl;
}

int main()
{
    for (int i = 0; i < 10; ++i)
        foo();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i)
        foo();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Calc crc with for-loop, cost ";
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i)
        go foo;
    boost::thread_group tg;
    for (int i = 0; i < 2; ++i)
        tg.create_thread([]{
                g_Scheduler.RunUntilNoTask();
                });
    tg.join_all();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Calc crc with coroutine, 2 threads, cost ";
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;

    return 0;
}

