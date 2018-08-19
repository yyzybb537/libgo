#pragma once
#include "../common/config.h"
#include "../common/clock.h"
#include "../sync/channel.h"

namespace co {

// 连接池
// @typename Connection: 连接类型
template <typename Connection>
class ConnectionPool
{
public:
    typedef std::shared_ptr<Connection> ConnectionPtr;
    typedef std::function<Connection*()> Factory;
    typedef std::function<void(Connection*)> Deleter;
    typedef std::function<bool(Connection*)> CheckAlive;

    // @Factory: 创建连接的工厂
    // @Deleter: 销毁连接, 传递NULL时会使用delete删除连接.
    // @maxConnection: 最大连接数, 0表示不限数量
    // @maxIdleConnection: 最大空闲连接数, 0表示不限数量
    // 注意：Factory和Deleter的调用可能会并行.
    explicit ConnectionPool(Factory f, Deleter d = NULL, size_t maxConnection = 0, size_t maxIdleConnection = 0)
        : factory_(f), deleter_(d), count_(0),
        maxConnection_(maxConnection),
        maxIdleConnection_(maxIdleConnection == 0 ? maxConnection : maxIdleConnection),
        channel_(maxIdleConnection_)
    {
        if (maxIdleConnection_ == 0)
            maxIdleConnection_ = maxConnection_;

        if (!deleter_) {
            deleter_ = [](Connection * ptr){ delete ptr; };
        }
    }

    ~ConnectionPool()
    {
        Connection* ptr = nullptr;
        while (channel_.TryPop(ptr)) {
            deleter_(ptr);
        }
    }

    // 预创建一些连接
    // @nConnection: 连接数量, 大于maxIdleConnection_的部分无效
    void Reserve(size_t nConnection)
    {
        for (size_t i = Count(); i < maxIdleConnection_; ++i)
        {
            Connection* connection = CreateOne();
            if (!connection) break;
            if (!channel_.TryPush(connection)) break;
        }
    }

    // 获取一个连接
    // 如果池空了并且连接数达到上限, 则会等待
    // 返回的智能指针销毁时, 会自动将连接归还给池
    ConnectionPtr Get(CheckAlive checkAliveOnGet = NULL,
            CheckAlive checkAliveOnPut = NULL)
    {
        Connection* connection = nullptr;
retry_get:
        if (channel_.TryPop(connection)) {
            if (checkAliveOnGet && !checkAliveOnGet(connection)) {
                deleter_(connection);
                connection = nullptr;
                goto retry_get;
            }

            return Out(connection, checkAliveOnPut);
        }

        connection = CreateOne();
        if (connection)
            return Out(connection, checkAliveOnPut);

        channel_ >> connection;
        if (checkAliveOnGet && !checkAliveOnGet(connection)) {
            deleter_(connection);
            connection = nullptr;
            goto retry_get;
        }

        return Out(connection, checkAliveOnPut);
    }

    // 获取一个连接
    // 如果池空了并且连接数达到上限, 则会等待
    // 返回的智能指针销毁时, 会自动将连接归还给池
    //
    // @timeout: 等待超时时间, 仅在协程中有效, 例: std::chrono::seconds(1)
    // @checkAliveOnGet: 申请时检查连接是否还有效
    // @checkAliveOnPut: 归还时检查连接是否还有效
    ConnectionPtr Get(FastSteadyClock::duration timeout,
            CheckAlive checkAliveOnGet = NULL,
            CheckAlive checkAliveOnPut = NULL)
    {
        FastSteadyClock::time_point deadline = FastSteadyClock::now() + timeout;
        Connection* connection = nullptr;
retry_get:
        if (channel_.TryPop(connection)) {
            if (checkAliveOnGet && !checkAliveOnGet(connection)) {
                deleter_(connection);
                connection = nullptr;
                goto retry_get;
            }

            return Out(connection, checkAliveOnPut);
        }

        connection = CreateOne();
        if (connection)
            return Out(connection, checkAliveOnPut);

        if (channel_.TimedPop(connection, deadline)) {
            if (checkAliveOnGet && !checkAliveOnGet(connection)) {
                deleter_(connection);
                connection = nullptr;
                goto retry_get;
            }

            return Out(connection, checkAliveOnPut);
        }

        return ConnectionPtr();
    }

    size_t Count()
    {
        return count_;
    }

private:
    Connection* CreateOne()
    {
        if (++count_ >= maxConnection_) {
            --count_;
            return nullptr;
        }

        return factory_();
    }

    void Put(Connection* connection)
    {
        if (!channel_.TryPush(connection)) {
            --count_;
            deleter_(connection);
        }
    }

    ConnectionPtr Out(Connection* connection, CheckAlive checkAliveOnPut)
    {
        return ConnectionPtr(connection, [this, checkAliveOnPut](Connection* ptr){
                    if (checkAliveOnPut && !checkAliveOnPut(ptr)) {
                        this->deleter_(ptr);
                        return ;
                    }

                    this->Put(ptr);
                });
    }

private:
    Factory factory_;
    Deleter deleter_;
    std::atomic<size_t> count_;
    size_t maxConnection_;
    size_t maxIdleConnection_;
    Channel<Connection*> channel_;
};

} // namespace co
