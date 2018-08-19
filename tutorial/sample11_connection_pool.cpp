/************************************************
 * libgo sample11 connection-pool
************************************************
 * 线程安全的连接池
 *
 * 鉴于有很多类似于MySQL这种连接必须独享的场景,
 * libgo提供了一个连接池的功能, 可以和协程池完美搭配.
************************************************/
#include "coroutine.h"
#include "win_exit.h"
using namespace std;

// 自定义一个连接类型
struct MySQLConnection
{
    int query(const char* sqlStatement) { return 0; }

    bool isAlive() { return true; }
};

bool checkAlive(MySQLConnection* c) {
    return c->isAlive();
}

int main(int argc, char** argv)
{
    // 创建一个连接池
    // @Factory: 创建连接的工厂
    // @Deleter: 销毁连接, 传递NULL时会使用delete删除连接.
    // @maxConnection: 最大连接数, 0表示不限数量
    // @maxIdleConnection: 最大空闲连接数, 0表示不限数量
    // 注意：Factory和Deleter的调用可能会并行.
    //   ConnectionPool(Factory f, Deleter d = NULL, size_t maxConnection = 0, size_t maxIdleConnection = 0)
    co::ConnectionPool<MySQLConnection> cPool(
            []{ return new MySQLConnection; },
            NULL, 
            100, // 限制最大100个连接, 防止把mysql冲垮
            20); // 最多保留20条空闲连接

    // Reserve接口可以预创建一些连接, 不能超过最大空闲连接数, 即: 20
    cPool.Reserve(10);

    // 获取一个连接, 无限期等待
    // @checkAliveOnGet: 申请时检查连接是否还有效, 默认NULL表示不检查
    // @checkAliveOnPut: 归还时检查连接是否还有效, 默认NULL表示不检查
    // @return: 返回MySQLConnection的智能指针, 引用计数归零时自动将连接还回池里.
    auto connPtr = cPool.Get();
    if (connPtr) {
        printf("get connection ok.\n");
    }

    // 主动归还
    connPtr.reset();

    // 获取和归还时检测连接有效性
    connPtr = cPool.Get(&checkAlive, &checkAlive);
    if (connPtr) {
        printf("get connection ok.\n");
    }

    connPtr.reset();

    // 获取一个连接, 等待一段时间, 如果没有可用连接就返回一个空指针
    // @timeout: 超时时间
    // @checkAliveOnGet: 申请时检查连接是否还有效, 默认NULL表示不检查
    // @checkAliveOnPut: 归还时检查连接是否还有效, 默认NULL表示不检查
    // @return: 返回MySQLConnection的智能指针, 引用计数归零时自动将连接还回池里.
    // 注意：在原生线程中使用时, 超时时长是不生效的.
    go [&]{
        connPtr = cPool.Get(std::chrono::milliseconds(100));
        if (connPtr) {
            printf("get connection ok.\n");
        }

        co_sched.Stop();
    };

    co_sched.Start();
    return 0;
}

