#include <hiredis/async.h>
#include "coroutine.h"
#include <vector>
#include <string>
#include <poll.h>
#include <sys/socket.h>

class AsyncRedisContext
{
public:
    AsyncRedisContext();
    ~AsyncRedisContext();

    // returns: error info.
    bool Connect(std::string host, int16_t port, int timeout = 10000);

    std::vector<std::string> CallCommand(std::vector<std::string> const& args);

private:
    static void OnConnectG(redisAsyncContext const* ac, int status);
    void OnConnect(int status);
    static void OnDisconnectG(redisAsyncContext const* ac, int status);
    void OnDisconnect(int status);
    static void RedisCallbackG(struct redisAsyncContext* ac, void* reply, void* privdata);
    void RedisCallback(void* reply, void* privdata);

private:
    redisAsyncContext *ac_ = nullptr;
    int connect_status_;
    co_chan<void> exit_w_;
    co_chan<void> exit_r_;
};

static void FakeFreeReplyObject(void*) {}

AsyncRedisContext::AsyncRedisContext()
{
}

AsyncRedisContext::~AsyncRedisContext()
{
    if (ac_)
        ::shutdown(ac_->c.fd, SHUT_RDWR);
    printf("destruct begin\n");
    exit_w_ << nullptr;
    printf("destruct begin 2\n");
    exit_r_ << nullptr;
    printf("destruct end\n");
}

bool AsyncRedisContext::Connect(std::string host, int16_t port, int timeout)
{
    co_chan<bool> result;

    go [=] {
        ac_ = redisAsyncConnect(host.c_str(), port);
        if (ac_->err) {
            redisAsyncFree(ac_);
            ac_ = nullptr;
            result << false;
            return ;
        }

        redisAsyncSetConnectCallback(ac_, &AsyncRedisContext::OnConnectG);
        redisAsyncSetDisconnectCallback(ac_, &AsyncRedisContext::OnDisconnectG);
        ac_->data = this;

        pollfd fds;
        fds.fd = ac_->c.fd;
        fds.events = POLLOUT | POLLERR | POLLHUP;
        int n = poll(&fds, 1, timeout);
        if (n <= 0) {
            redisAsyncFree(ac_);
            ac_ = nullptr;
            result << false;
            return ;
        }

        connect_status_ = REDIS_ERR;
        redisAsyncHandleWrite(ac_);
        result << (connect_status_ == REDIS_OK);
        return ;
    };

    bool ret;
    result >> ret;
    printf("connect ok\n");
    if (ret) {
        ac_->c.reader->fn->freeObject = &FakeFreeReplyObject;

        // write loop
        go [this]{
            while (!exit_w_.TryPop(nullptr)) {
                pollfd fds;
                fds.fd = ac_->c.fd;
                fds.events = POLLOUT | POLLERR | POLLHUP;
                int n = poll(&fds, 1, -1);
                if (n <= 0)
                    continue;

                redisAsyncHandleWrite(ac_);
            }

            printf("exit write loop\n");
        };

        // recv loop
        go [this]{
            printf("recv 1\n");
            while (!exit_r_.TryPop(nullptr)) {
                printf("recv 2\n");
                pollfd fds;
                fds.fd = ac_->c.fd;
                fds.events = POLLIN | POLLERR | POLLHUP;
                int n = poll(&fds, 1, -1);
                if (n <= 0)
                    continue;

                printf("recv 3\n");
                redisAsyncHandleRead(ac_);
                printf("recv 4\n");
            }

            printf("exit recv loop\n");
        };
    }

    return ret;
}

void AsyncRedisContext::OnConnectG(redisAsyncContext const* ac, int status)
{
    AsyncRedisContext* self = (AsyncRedisContext*)ac->data;
    self->OnConnect(status);
}

void AsyncRedisContext::OnConnect(int status)
{
    connect_status_ = status;
}

void AsyncRedisContext::OnDisconnectG(redisAsyncContext const* ac, int status)
{
    AsyncRedisContext* self = (AsyncRedisContext*)ac->data;
    self->OnDisconnect(status);
}

void AsyncRedisContext::OnDisconnect(int status)
{
    printf("disconnected!\n");
    //exit_channel_ << nullptr << nullptr;
}

std::vector<std::string> AsyncRedisContext::CallCommand(const std::vector<std::string> & args)
{
    std::vector<std::string> result;
    if (!ac_) return result;

    std::vector<const char*> c_args;
    for (auto &arg : args)
        c_args.push_back(arg.c_str());

    co_chan<redisReply*> c;
    size_t size = c_args.size();
    printf("call redisAsyncCommandArgv\n");
    if (-1 == redisAsyncCommandArgv(ac_, &AsyncRedisContext::RedisCallbackG, &c,
                size, c_args.data(), &size)) {
        return result;
    }

    printf("wait reply\n");
    redisReply *reply = nullptr;
    c >> reply;
    if (!reply)
        return result;

    // parse
    printf("receive reply:%p\n", reply);

    freeReplyObject(reply);
    return result;
}
void AsyncRedisContext::RedisCallbackG(struct redisAsyncContext* ac, void* reply, void* privdata)
{
    AsyncRedisContext* self = (AsyncRedisContext*)ac->data;
    self->RedisCallback(reply, privdata);
}
void AsyncRedisContext::RedisCallback(void* reply, void* privdata)
{
    co_chan<redisReply*> &c = *(co_chan<redisReply*> *)privdata;
    c << (redisReply*)reply;
}

void call_redis()
{
    AsyncRedisContext stack_c;
    AsyncRedisContext *c = &stack_c;
    if (!c->Connect("127.0.0.1", 6379)) {
        printf("connect error");
    }

//    std::vector<std::string> cmd{"dbsize"};
//    c->CallCommand(cmd);
//    printf("done\n");
}

co_main()
{
    co_sched.GetOptions().debug = co::dbg_syncblock | co::dbg_hook | co::dbg_switch;
    go call_redis;
    return 0;
}
