#include <libgo/libgo.h>
#include <chrono>
#include <iostream>
#include <atomic>
#include <inttypes.h>
#include <sys/syscall.h>
#include <errno.h>
#include <map>

using namespace std;
using namespace std::chrono;

const int nWork = 100;

//#ifdef errno
//extern int *__errno_location (void);
//extern "C" volatile int * my__errno_location (void)
//{
//    std::atomic_thread_fence(std::memory_order_seq_cst);
//    printf("hook errno in cpp!\n");
//    return (volatile int*)__errno_location();
//}
//# undef errno
//# define errno (*my__errno_location())
//#endif

std::mutex g_mtx;
std::map<volatile void*, int> g_addrs;

void check(int threadId, volatile void* errnoAddr)
{
    std::unique_lock<std::mutex> lock(g_mtx);
    if (!g_addrs.count(errnoAddr)) {
        g_addrs[errnoAddr] = threadId;
        return ;
    }

    if (g_addrs[errnoAddr] != threadId) {
        printf("threadId=%d  &errno=[%p]\n", threadId, errnoAddr);
        printf("error!\n");
        exit(1);
    }
}

// 大计算量的函数
int c = 0;
std::atomic<int> done{0};
void foo(int work_id)
{
    int v = (int)rand();
    for (int i = 1; i < 20000000; ++i) {
        if (i == 1000)
        {
            errno = 1;
            int t1 = syscall(SYS_gettid);
            fprintf(stdout, "1- work id: [%d]- errno=%d addr: [%p], thread id: [%d]\n", work_id, errno, &errno, t1);

            check(t1, &errno);

            co_yield;

            int t2 = syscall(SYS_gettid);
            errno = 2;
            (void)::write(10000, "a", 1);
            fprintf(stdout, "2- work id: [%d]- errno=%d addr: [%p], thread id: [%d]\n", work_id, errno, &errno, t2);
            check(t2, &errno);

        }
        v *= i;
    }
    c += v;

    if (++done == nWork * 2)
        co_sched.Stop();
}

int main()
{
//#if __ASSEMBLER__
//    printf("defined __ASSEMBLER__\n");
//#else
//    printf("not defined __ASSEMBLER__\n");
//#endif
//    return 0;

    // 编写cpu密集型程序时, 可以延长协程执行的超时判断阈值, 避免频繁的worksteal产生
    co_opt.cycle_timeout_us = 1 * 1000;

    // 普通的for循环做法
    auto start = system_clock::now();
    // for (int i = 0; i < nWork; ++i)
    //     foo();
    auto end = system_clock::now();
    cout << "for-loop, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;

    // 使用libgo做并行计算
    start = system_clock::now();
    for (int i = 0; i < nWork; ++i)
        go [i] { foo(i); };

    // 创建8个线程去并行执行所有协程 (由worksteal算法自动做负载均衡)
    co_sched.Start(8);

    end = system_clock::now();
    cout << "go with coroutine, cost ";
    cout << duration_cast<milliseconds>(end - start).count() << "ms" << endl;
	cout << "result zero:" << c * 0 << endl;
    return 0;
}

