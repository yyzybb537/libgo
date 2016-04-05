#include "sample12_hiredis.h"
#include <sys/socket.h>

std::error_code MakeRedisErrorCode(int code)
{
    static redis_error_category category;
    return std::error_code(code, category);
}

RedisPipeline::~RedisPipeline()
{
    if (establish_) {
        establish_ = false;
        shutdown_c_ << nullptr << nullptr;
        ::shutdown(ctx_->fd, SHUT_RDWR);
        write_condition_.TryPush(nullptr);
        exit_c_ >> nullptr >> nullptr;
        redisFree(ctx_);
        ctx_ = nullptr;
    }

    std::pair<reply_t, std::error_code> rt_pair;
    rt_pair.second = MakeRedisErrorCode(err_redis_eof);
    for (auto &c : pipeline_)
        c << rt_pair;

//    printf("RedisPipeline destruct\n");
}
bool RedisPipeline::Connect(const std::string & host, int16_t port, int timeout_ms)
{
    if (establish_) return false;
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    ctx_ = redisConnectWithTimeout(host.c_str(), port, tv);
    if (!ctx_) return false;

    go [this] {
        this->ReadLoop();
//        printf("ReadLoop end\n");
        exit_c_ << nullptr;
    };

    go [this] {
        this->WriteLoop();
//        printf("WriteLoop end\n");
        exit_c_ << nullptr;
    };
    establish_ = true;
    return true;
}
RedisPipeline::reply_t RedisPipeline::Call(std::string const& command, std::error_code * ec)
{
    if (!establish_) {
        if (ec) *ec = MakeRedisErrorCode(err_redis_not_estab);
        return reply_t();
    }

    if (command.empty()) {
        if (ec) *ec = MakeRedisErrorCode(err_redis_invalid_request);
        return reply_t();
    }

    co_chan<std::pair<reply_t, std::error_code>> c(1);
    std::unique_lock<co_mutex> lock(pipeline_mutex_);
    pipeline_.push_back(c);
    if (REDIS_ERR == redisAppendCommand(ctx_, command.c_str())) {
        if (ec) *ec = MakeRedisErrorCode(ctx_->err);
        pipeline_.pop_back();
        return reply_t();
    }
    lock.unlock();
    if (!writing_)
        write_condition_.TryPush(nullptr);
    std::pair<reply_t, std::error_code> reply_pair;
    c >> reply_pair;
    if (reply_pair.second && ec)
        *ec = reply_pair.second;
    return reply_pair.first;
}
RedisPipeline::reply_t RedisPipeline::Call(request_t const& request, std::error_code * ec)
{
    if (!establish_) {
        if (ec) *ec = MakeRedisErrorCode(err_redis_not_estab);
        return reply_t();
    }

    if (request.empty()) {
        if (ec) *ec = MakeRedisErrorCode(err_redis_invalid_request);
        return reply_t();
    }

    std::string command;
    for (auto &elem : request)
        command += elem, command += " ";
    command.pop_back();
    return Call(command, ec);
}
bool RedisPipeline::Convert(reply_t & rt, redisReply *reply)
{
    switch (reply->type) {
        case REDIS_REPLY_ARRAY: 
            {
                for (size_t i = 0; i < reply->elements; ++i)
                    if (!Convert(rt, reply->element[i]))
                        return false;
            } 
            break;
        case REDIS_REPLY_STRING:
            {
                rt.push_back(std::string(reply->str, reply->len));
            } 
            break;
        case REDIS_REPLY_NIL:
            {
                rt.push_back("");
            } 
            break;
        case REDIS_REPLY_INTEGER:
        case REDIS_REPLY_STATUS:
            {
                rt.push_back(std::to_string(reply->integer));
            }
            break;
        case REDIS_REPLY_ERROR:
            {
                rt.push_back(std::string(reply->str, reply->len));
                return false;
            }
            break;
        default:
            return false;
    }

    return true;
}
void RedisPipeline::ReadLoop()
{
    while (!shutdown_c_.TryPop(nullptr)) {
        int res = redisBufferRead(ctx_);
        if (REDIS_ERR == res) {
            printf("redisBufferRead eof: %s\n", ctx_->errstr);
            return ;
        }

        for (;;) {
            void *r = nullptr;
            res = redisReaderGetReply(ctx_->reader, &r);
            if (REDIS_ERR == res) {
                printf("parse reply error\n");
                return ;
            }

            if (!r) break;

//            printf("------- recv reply -------\n");
            co_chan<std::pair<reply_t, std::error_code>> c = pipeline_.front();
            pipeline_.pop_front();
            std::pair<reply_t, std::error_code> rt_pair;

            if (!Convert(rt_pair.first, (redisReply*)r)) {
                rt_pair.second = MakeRedisErrorCode(err_redis_command_error);
            }
            c << std::move(rt_pair);
        }
    }
}
void RedisPipeline::WriteLoop()
{
    while (!shutdown_c_.TryPop(nullptr)) {
        int done;
        if (REDIS_ERR == redisBufferWrite(ctx_, &done)) {
            printf("redisBufferWrite error\n");
            return ;
        }

        if (done) {
            writing_ = false;
            write_condition_ >> nullptr;
            writing_ = true;
        }
    }
}

void call_redis()
{
    redisContext* ctx = redisConnect("127.0.0.1", 6379);
    if (!ctx) {
        printf("connect error\n");
        return ;
    }

    for (int i = 0; i < 2; ++i)
        if (REDIS_ERR == redisAppendCommand(ctx, "hgetall ahash")) {
            printf("append command error\n");
            return ;
        }

    int done;
    for (;;) {
        if (REDIS_ERR == redisBufferWrite(ctx, &done)) {
            printf("redisBufferWrite error\n");
            return ;
        }

        if (done) break;
    }

    for (;;) {
        int res = redisBufferRead(ctx);
        if (REDIS_ERR == res) {
            printf("redisBufferRead error\n");
            return ;
        }

        for (;;) {
            void *r = nullptr;
            res = redisReaderGetReply(ctx->reader, &r);
            if (REDIS_ERR == res) {
                printf("parse reply error\n");
                return ;
            }

            if (!r) break;

            printf("------- recv reply -------\n");

            redisReply *reply = (redisReply *)r;
            printf("reply type: %d\n", reply->type);
            if (reply->type == REDIS_REPLY_INTEGER) {
                printf("reply integer: %lld\n", reply->integer);
            } else if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    redisReply *child = reply->element[i];
                    printf("reply[%d]: %.*s\n", (int)i, child->len, child->str);
                }
            }
        }
    }
}

void test_call(RedisPipeline &r, std::string cmd, bool output = true)
{
    if (output)
        printf("call %s\n", cmd.c_str());
    std::error_code ec;
    RedisPipeline::reply_t reply = r.Call(cmd, &ec);
    if (ec) {
        printf("error: %d:%s:%s\n", ec.value(), ec.message().c_str(), reply[0].c_str());
        return ;
    }

    if (output)
        for (size_t i = 0; i < reply.size(); ++i) {
            printf("reply[%d]: %s\n", (int)i, reply[i].c_str());
        }
}

void call_redis2()
{
    RedisPipeline r;
    if (!r.Connect("127.0.0.1", 6379)) {
        printf("connect error\n");
        return ;
    }

//    co_chan<void> exit_c;
//    go [=, &r] { test_call(r, "hgetall ahash"); exit_c << nullptr; };
    test_call(r, "hgetall hash");
    test_call(r, "hget ahash");
    test_call(r, "dbsize");
//    exit_c >> nullptr;
}

void call_redis3()
{
    RedisPipeline r;
    if (!r.Connect("127.0.0.1", 6379)) {
        printf("connect error\n");
        return ;
    }

    const int count = 10000;
    const int test_seconds = 3;
    co_chan<void> exit_c(count);
    bool stop = false;
    long long int total = 0;
    for (int i = 0; i < count; ++i) {
        go [&, exit_c] {
            while (!stop) {
                test_call(r, "hgetall ahash", false);
                ++total;
            }
            exit_c << nullptr;
        };
    }

    go [&] {
        ::sleep(test_seconds);
        stop = true;
    };

    for (int i = 0; i < count; ++i)
        exit_c >> nullptr;

    printf("%d seconds call redis %lld times.\n", test_seconds, total);
    printf("QPS: %lld.\n", total / test_seconds);
}

co_main()
{
//    co_sched.GetOptions().debug = co::dbg_syncblock | co::dbg_hook | co::dbg_switch;
    printf("--------------- correct test ---------------\n");
    go call_redis2;
    printf("--------------------------------------------\n");

    printf("--------------- benchmark test ---------------\n");
    go call_redis3;
    printf("--------------------------------------------\n");
    return 0;
}
