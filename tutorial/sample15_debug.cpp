/************************************************
 * libgo sample15 debug
*************************************************/
#include "coroutine.h"
#include "win_exit.h"
#include <stdio.h>

void foo()
{
    printf("function pointer\n");
}

int main()
{
    //----------------------------------
    // 使用关键字go创建协程, go后面可以使用:
    //     1.void(*)()函数指针, 比如:foo.
    //     2.也可以使用无参数的lambda, std::bind对象, function对象, 
    //     3.以及一切可以无参调用的仿函数对象
    //   注意不要忘记句尾的分号";".
    go foo;

    for (int i = 0; i < 4; ++i)
        go []{
            co_sleep(100);
            printf("lambda\n");
        };

    go []{
        printf("%s\n", co::CoDebugger::getInstance().GetAllInfo().c_str());
    };

    go []{
        co_sleep(50);
        printf("%s\n", co::CoDebugger::getInstance().GetAllInfo().c_str());
    };

    // 200ms后安全退出
    std::thread([]{ co_sleep(200); co_sched.Stop(); }).detach();

    co_sched.Start();
    return 0;
}

