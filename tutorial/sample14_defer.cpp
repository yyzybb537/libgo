/************************************************
 * libgo sample14: defer
************************************************
 * 像golang一样, libgo也提供了一个co_defer关键字
 * 用来延迟执行一些操作.
 * co_defer的语法和go关键字一样, 也是后面跟一个function.
 * 例如:
 *    void foo();
 *
 *    co_defer foo;
 *
 * 同时还提供了一个宏: co_defer_scope, 可以直接在
 * 参数中写多条语句.
 * 例如:
 *    co_defer_scope {
 *      cout << "1" << endl;
 *      cout << "2" << endl;
 *    };
 *
 * 除此之外, libgo还提供了撤销defer操作的办法:
 * 在紧随defer定义之后, 使用co_last_defer()可以获
 * 取到刚刚定义的defer对象, 然后调用dismiss接口.
 * 例如:
 *    co_defer foo;
 *    co_last_defer().dismiss();
 *
 * 也可以暂存起来延迟撤销:
 *    co_defer foo;
 *    auto & defer_foo_object = co_last_defer();
 *    defer_foo_object.dismiss();
 *
 * 注: 1.defer在协程中和协程外均可使用
 *     2.和go关键字一样，不要忘记尾部的分号
************************************************/
#include <unistd.h>
#include <iostream>
#include <libgo/libgo.h>
#include "win_exit.h"
using namespace std;

void foo() {
    auto & dis1 = co_last_defer();
    {
        co_defer_scope {
            cout << "go defer 1" << endl;
        };
        auto & dis2 = co_last_defer();
        bool success = dis2.dismiss();
        cout << "IsOK:" << success << endl;
    }
    auto & dis3 = co_last_defer();
    bool success = dis3.dismiss();
    cout << "IsOK:" << !success << endl;
    cout << "IsOK:" << (&dis1 == &dis3) << endl;
}

int main() {
    co_defer [&]{ cout << "defer 3" << endl; };

    co_defer_scope {
        cout << "defer 1" << endl;
        cout << "defer 2" << endl;
    };

    co_defer []{ cout << "cancel 1" << endl; };
    auto & defer_obj = co_last_defer();
    defer_obj.dismiss();

    co_defer []{ cout << "cancel 2" << endl; };
    co_last_defer().dismiss();

    go foo;

    // 200ms后安全退出
    std::thread([]{ co_sleep(200); co_sched.Stop(); }).detach();

    co_sched.Start();
    return 0;
}

