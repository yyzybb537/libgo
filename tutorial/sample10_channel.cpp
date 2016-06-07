/************************************************
 * libgo sample10
************************************************
 * 在编写比较复杂的网络程序时，经常需要在多个协程
 * 间传递数据，此时就需要用到channel。
 *
************************************************/
#include "coroutine.h"
#include "win_exit.h"

struct A
{
    // 可以由int隐式转换来构造
    A(int i) : i_(i) {}
    // 可以隐式转换成int
    operator int() { return i_; }
    int i_;
};

int main(int argc, char** argv)
{
    /*********************** 1. 基本使用 ************************/
    // Channel也是一个模板类,
    // 使用以下代码将创建一个无缓冲区的、用于传递整数的Channel：
    co_chan<int> ch_0;

    // channel是引用语义, 在协程间共享直接copy即可.
    go [=]{
        // 在协程中, 向ch_0写入一个整数1.
        // 由于ch_0没有缓冲区, 因此会阻塞当前协程, 直到有人从ch_0中读取数据:
        ch_0 << 1;
    };

    go [=] {
        // Channel是引用计数的, 复制Channel并不会产生新的Channel, 只会引用旧的Channel.
        // 因此, 创建协程时可以直接拷贝Channel.
        // Channel是mutable的, 因此可以直接使用const Channel读写数据, 
        // 这在使用lambda表达式时是极为方便的特性.
        
        // 从ch_0中读取数据:
        int i;
        ch_0 >> i;
        printf("i = %d\n", i);
    };
    co_sched.RunUntilNoTask();

    /*********************** 2. 带缓冲区的Channel ************************/
    // 创建缓冲区容量为1的Channel, 传递智能指针:
    co_chan<std::shared_ptr<int>> ch_1(1);

    go [=] {
        std::shared_ptr<int> p1(new int(1));

        // 向ch_1中写入一个数据, 由于ch_1有一个缓冲区空位, 因此可以直接写入而不会阻塞当前协程.
        ch_1 << p1;
        
        // 再次向ch_1中写入整数2, 由于ch_1缓冲区已满, 因此阻塞当前协程, 等待缓冲区出现空位.
        ch_1 << p1;
    };

    go [=] {
        std::shared_ptr<int> ptr;

        // 由于ch_1在执行前一个协程时被写入了一个元素, 因此下面这个读取数据的操作会立即完成.
        ch_1 >> ptr;

        // 由于ch_1缓冲区已空, 下面这个操作会使当前协程放弃执行权, 等待第一个协程写入数据完成.
        ch_1 >> ptr;
        printf("*ptr = %d\n", *ptr);
    };
    co_sched.RunUntilNoTask();

    /*********************** 3. 支持隐式转换 ************************/
    // Channel在写入数据和读取数据时均支持隐式转换
    co_chan<A> ch_a(1);
    int i = 0;
    // Channel在协程外部也可以使用, 不过阻塞行为会阻塞当前线程 (while sleep的方式, 而不是挂起),
    // 因此不推荐在协程外部使用.
    ch_a << 1;
    ch_a >> i;
    printf("i = %d\n", i);

    /*********************** 4. 支持移动语义 ************************/
    // Channel完整地支持移动语义, 因此可以存储类似std::unique_ptr这种不可拷贝但支持移动语义的对象.
    co_chan<std::unique_ptr<int>> ch_move(1);
    std::unique_ptr<int> uptr(new int(1)), sptr;
    ch_move << std::move(uptr);
    ch_move >> sptr;
    printf("*sptr = %d\n", *sptr);
    
    /*********************** 5. 多读多写 ************************/
    // Channel可以同时由多个线程读写.

    /*********************** 6. 线程安全 ************************/
    // Channel是线程安全的, 因此不必担心在多线程调度协程时会出现问题.

    return 0;
}

