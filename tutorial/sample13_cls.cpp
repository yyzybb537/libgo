/************************************************
 * libgo sample13
************************************************
 * 线程有TLS(thread local storage),
 * libgo的协程也有CLS(coroutine local storage)
 * 同样支持TLS的三种使用场景：
    1.块作用域(函数内)
    2.全局作用域
    3.类静态成员变量
 * 不仅如此, 还支持非静态成员变量
 * 声明一个cls变量使用宏: co_cls
 * Eg:
     int& var = co_cls(int);
     或
     co_cls_ref(int) var = co_cls(int);

 * 块作用域中推荐使用第一种写法, 免掉了一次隐士转换, 更便于使用
 * 第一种写法一定注意不要忘记引用符&
 *
 * 全局作用域\类成员变量只能使用第二种写法, 并且会有编译warning，
 * 请勿开启-Werror选项!
 *
 * co_cls_ref(int)定义了一个可以隐式转换成int&的模板类,
 * 如果此处不是int而是自定义类, 要访问类的成员或函数, 
 * 需要手动转换一次:
 * Eg:
     co_cls_ref(MyClass) ref = co_cls(MyClass);
     MyClass & var = ref;
     var.xxx();
 
 * co_cls宏还可以传入任意数量的初始化参数:
 * Eg:
     int& var = co_cls(int, 10);
************************************************/
#include <unistd.h>
#include <iostream>
#include <libgo/libgo.h>
#include "win_exit.h"
using namespace std;

struct A {
    A() { cout << "A construct" << endl; }
    ~A() { cout << "A destruct" << endl; }
    int i = 0;
};

void foo() {
    A& a = co_cls(A);
    cout << "foo -> " << a.i << endl;
    a.i++;
}

void foo3() {
    for (int i = 0; i < 3; i++)
        foo();
}

struct B {
    static co_cls_ref(int) a;
};
co_cls_ref(int) B::a = co_cls(int);

void bar() {
    B::a += 10;
    cout << "bar -> " << B::a << endl;
}

auto gCls = co_cls(int);
void car() {
    cout << "car -> " << gCls << endl;
    gCls++;
}

void oneLine() {
    int &a = co_cls(int, 0), &b = co_cls(int, 10);
    int &c = co_cls(int, 20);
    cout << "oneLine -> "<< "a=" << a << ", b=" << b << ", c=" << c << endl;
}

void test() {
    cout << " ---- function ---- " << endl;
    go foo3;

    foo();
    foo();

    cout << " ---- static member ---- " << endl;
    go bar;
    bar();
    bar();

    cout << " ---- global ---- " << endl;
    go car;
    car();
    car();

    oneLine();

    co_sched.Stop();
}

int main() {
    go test;
    co_sched.Start();
}
