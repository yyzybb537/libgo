/************************************************
 * libgo sample6
************************************************
 * 在多线程编程中，你经常会使用到一些锁用来同步代码。
 * libgo目前还没有支持OS原生的锁的自动co_yield，
 * 不过libgo提供了一个协程锁co_mutex，可以达到
 * 同样的效果，并且在等待协程锁期间可以让出cpu，执行
 * 其他协程。
 *
 * co_mutex提供了与标准库的mutex相同的操作接口，可
 * 以轻松搭配标准库提供的lock_guard uniqe_lock等工
 * 具。
 *
 * 和标准库的mutex一样，使用时要注意生命期的控制，
 * 不要在已经析构的co_mutex上进行操作
************************************************/
#include <mutex>
#include "coroutine.h"
#include "win_exit.h"

int main()
{
    co_mutex cm;

    // 为展示co_mutex自动切换协程的功能，先锁住mutex
    cm.lock();

    go [&]{
        for (int i = 0; i < 3; ++i) {
            std::lock_guard<co_mutex> lock(cm);
            printf("coroutine 1 lock\n");
        }
    };

    go [&]{
        cm.unlock();
        for (int i = 0; i < 3; ++i) {
            std::lock_guard<co_mutex> lock(cm);
            printf("coroutine 2 lock\n");
        }
    };

    // 读写锁
    co_rwmutex m;

    go [&]{
        {
            // 读锁
            m.reader().lock();
            m.reader().unlock();

            // 或使用标准库提供的lock系列的工具
            // 读锁视图的类型为：co_rmutex
            std::unique_lock<co_rmutex> lock(m.Reader());
        }

        {
            // 写锁
            m.writer().lock();
            m.writer().unlock();

            // 或使用标准库提供的lock系列的工具
            // 写锁视图的类型为：co_wmutex
            std::unique_lock<co_wmutex> lock(m.Writer());
        }
    };

    // 200ms后安全退出
    std::thread([]{ co_sleep(200); co_sched.Stop(); }).detach();

    co_sched.Start();
    return 0;
}

