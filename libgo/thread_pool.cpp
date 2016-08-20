#include <libgo/thread_pool.h>
#include <unistd.h>
#include <thread>
#include <libgo/scheduler.h>
#include <libgo/error.h>

namespace co {

ThreadPool::~ThreadPool()
{
    TPElemBase *elem = nullptr;
    while ((elem = elem_list_.pop())) {
        delete elem;
    }
}

void ThreadPool::RunLoop()
{
    assert_std_thread_lib();
    for (;;)
    {
        TPElemBase *elem = get();
        if (!elem) continue;
        elem->Do();
        delete elem;
    }
}

TPElemBase* ThreadPool::get()
{
    TPElemBase *elem = elem_list_.pop();
    if (elem) return elem;

    std::unique_lock<std::mutex> lock(cond_mtx_);
    while (elem_list_.empty())
        cond_.wait(lock);
    return elem_list_.pop();
}

void ThreadPool::assert_std_thread_lib()
{
    static bool ass = __assert_std_thread_lib();
    (void)ass;
}
bool ThreadPool::__assert_std_thread_lib()
{
    std::mutex mtx;
    std::unique_lock<std::mutex> lock1(mtx);
    std::unique_lock<std::mutex> lock2(mtx, std::defer_lock);
    if (lock2.try_lock()) {
        // std::thread没有生效
        // 在gcc上, 静态链接请在链接时使用参数:
        //    -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -static
        //          动态链接请在编译和链接时都使用参数：
        //    -pthread
        //    注意：是-pthread, 而不是-lpthread.
        ThrowError(eCoErrorCode::ec_std_thread_link_error);
    }

//    try {
//        int v = 0;
//        std::thread t([&]{ v = 1; });
//        t.detach();
//        if (!v)
//            ThrowError(eCoErrorCode::ec_std_thread_link_error);
//    } catch (std::exception & e) {
//        ThrowError(eCoErrorCode::ec_std_thread_link_error);
//    }
    return true;
}

} //namespace co

