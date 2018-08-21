/************************************************
 * libgo sample10 coroutine-pool
************************************************
 * 如果你已经有一个异步回调的项目代码, 想要调用一些
 * 阻塞的操作, 比如: 访问DB, 读写文件, 执行一些耗时比较久的计算.
 * 同时又不想让这些阻塞的操作堵住主循环.
 *
 * libgo为这种场景量身打造了无侵入式的优雅解决方案: 连接池+协程池
 * 本节先来介绍协程池, 协程池是用来替代传统的线程池方案的, 相比传
 * 统的线程池方案有以下几点改进:
 *
 *    1.协程池中执行阻塞网络IO不会卡住线程, 用同样的线程数
 *      可以实现更高的并发
 *    2.协程池的调度线程数可以动态伸缩, 不必担心寥寥数个长
 *      时间阻塞的操作堵死整个池
 *    3.提供Callback和Channel两种完成通知模式, 无论是从协程
 *      中投递任务、还是从原生的异步回调线程中投递任务、还是
 *      从同步模型的原生线程中投递任务, 都可以无缝衔接.
 * 
************************************************/
#include "coroutine.h"
#include "win_exit.h"
using namespace std;

void foo()
{
    printf("do some block things in co::AsyncCoroutinePool.\n");
}

void done()
{
    printf("done.\n");
}

int calc() {
    return 1024;
}

void callback(int val) {
    printf("calc result: %d\n", val);
}

int main(int argc, char** argv)
{
    //-----------------------------------------------
    // 创建一个协程池
    co::AsyncCoroutinePool * pPool = co::AsyncCoroutinePool::Create();

    // 可以自定义协程池中的最大协程数,
    // 建议设置的多一些, 不被执行的协程不会占用cpu资源.
    pPool->InitCoroutinePool(1024);

    // 启动协程池
    // 第一个参数: 最小调度线程数
    // 第二个参数: 最大调度线程数
    // 有效的调度线程数总是保持与最小值相同, 仅当有调度线程被阻塞住时,
    // 才会动态扩展调度线程数.
    pPool->Start(4, 128);

    // 如果你希望控制处理完成回调所用的线程 (通常用于单线程程序), 可以绑定
    // 一个或多个回调处理器.
    // 如果不绑定回调处理器, 完成回调就直接在协程池中被调用.
    auto cbp = new co::AsyncCoroutinePool::CallbackPoint;
    pPool->AddCallbackPoint(cbp);  // 注意只能加入, 不能删除, 要保证处理器的生命期足够长.

    // 1.以回调的方式投递任务 (适用于异步回调模型的项目)
    pPool->Post(&foo, &done);

    // 2.还可以投递带返回值的回调函数, callback接受返回值, 以此形成回调链.
    pPool->Post<int>(&calc, &callback);

    // 循环执行回调处理器, 直到前面投递的任务完成为止.
    for (;;) {
        size_t trigger = cbp->Run();
        if (trigger > 0)
            break;
    }

    // 以Channel的方式投递任务 (适用于协程中, 或同步模型的原生线程)
    // channel方式等待任务完成时, callback不会经过回调处理器.
    co_chan<int> ch(1);
    pPool->Post<int>(ch, []{ return 8; });

    // 等待任务完成
    int val = 0;
    ch >> val;
    printf("val = %d\n", val);

    //-----------------------------------------------
    // 还可以投递void返回值类型的任务
    co_chan<void> ch2(1);
    pPool->Post(ch2, []{ printf("void task.\n"); });

    // 等待任务完成
    ch2 >> nullptr;
    //-----------------------------------------------
    return 0;
}

