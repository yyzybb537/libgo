/************************************************
 * libgo sample13
 ************************************************
 * 这是协程本地变量的例子 co_local
 *
 * 协程本地变量类似于线程本地变量 thread_local
 * 每个协程持有的本地变量对其他协程来说是不可见的
 *
 * 使用协程本地变量前需要先检查变量是否初始化，如果没有
 * 则需要调用构造函数进行初始化，协程结束时会自动调用
 * 变量的析构函数进行析构
 *
 * 详细请看下面的例子
 ************************************************/
#include "coroutine.h"
#include <iostream>

using namespace std;

struct AAA {
public:
	AAA(int a, const string& b) {
		this->a = a;
		this->b = b;
		cout << "AAA start a=" << a << ", b=" << b << endl;
	}

	void print() {
		cout << "AAA print a=" << a << ", b=" << b << endl;
	}

	~AAA() {
		cout << "AAA end a=" << a << ", b=" << b << endl;
	}

	int a;
	string b;
};

//全局协程本地变量
co_local<string> co_global_var(GET_CO_LOCAL_ID);

int main(int argc, char** argv) {
	//使用协程本地变量时需要指定一个全局唯一的ID
	//使用宏 GET_CO_LOCAL_ID 即可生成

	//推荐将 co_local 变量设置为 static
	//当然不设置也没有什么区别，但设置为static从语义上来看更加清晰

	co_local<int> co_var1(GET_CO_LOCAL_ID);
	static co_local<string> co_var2(GET_CO_LOCAL_ID);
	co_local<AAA> co_var3(GET_CO_LOCAL_ID);
	static co_local<AAA> co_var4(GET_CO_LOCAL_ID);

	//co_local不允许复制，只能传递指针或引用
	go[&] {
		//标准使用方式，先获取并检查是否已经初始化，如果没有则调用构造函数
		//推荐将co_local与协程监听器一同使用，可以监听协程onStart事件，在该事件中
		//将所有的协程本地变量初始化，在之后就直接使用就可以了
		int* var1 = co_var1.Get();
		if (!var1) {
			var1 = co_var1.Emplace(111);
		}

		//也可使用宏co_local_init进行初始化，与以上写法完全等价
		//注意后边提供给构造函数的参数列表只有当实际需要构造时才会真正求值
		string* var2 = co_local_init(co_var2, "aaa 222");
		AAA* var3 = co_local_init(co_var3, 333, "aaa 333");
		AAA* var4 = co_local_init(co_var4, 444, "aaa 444");

		string* g_var = co_local_init(co_global_var, "aaa global");

		for (int i = 0; i < 5; ++i) {
			cout << "task 1 "<<"var1=" << *var1 << endl;
			cout << "task 1 "<<"var2=" << *var2 << endl;

			cout << "task 1 "<<"var3 ";
			var3->print();

			cout << "task 1 "<<"var4 ";
			var4->print();

			cout << "task 1 "<<"g_var=" << *g_var << endl;

			co_sleep(500);
		}
		cout << "task 1 end" << endl;
	};

	go[&] {
		int* var1 = co_local_init(co_var1, 222);
		string* var2 = co_local_init(co_var2, "bbb 222");
		AAA* var3 = co_local_init(co_var3, 333, "bbb 333");
		AAA* var4 = co_local_init(co_var4, 444, "bbb 444");

		string* g_var = co_local_init(co_global_var, "bbb global");

		for (int i = 0; i < 5; ++i) {
			cout << "task 2 "<<"var1=" << *var1 << endl;
			cout << "task 2 "<<"var2=" << *var2 << endl;

			cout << "task 2 "<<"var3 ";
			var3->print();

			cout << "task 2 "<<"var4 ";
			var4->print();

			cout << "task 2 "<<"g_var=" << *g_var << endl;

			co_sleep(450);
		}
		cout << "task 2 end" << endl;
	};

	//协程运行完成后会自动调用对象的析构函数

	//注意协程本地变量不保证析构的顺序与构造的顺序正好相反
	//如果协程本地变量之间有引用关系请注意这一点

	//TODO:谁来补个英文说明

	co_sched.RunUntilNoTask();
	return 0;
}

