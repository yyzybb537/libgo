#include <hiredis/hiredis.h>
#include <libgo/coroutine.h>
#include <list>
#include <vector>
#include <string>
#include <mutex>
#include <system_error>

enum eRedisErrorType
{
    err_redis_ok = 0,
    err_redis_io = REDIS_ERR_IO,
    err_redis_other = REDIS_ERR_OTHER,
    err_redis_eof = REDIS_ERR_EOF,
    err_redis_protocol = REDIS_ERR_PROTOCOL,
    err_redis_oom = REDIS_ERR_OOM,
    err_redis_not_estab,
    err_redis_invalid_request,
    err_redis_command_error,
};

class redis_error_category : public std::error_category
{
public:
    const char* name() const noexcept
    {
        return "redis error";
    }

    std::string message(int value) const
    {
        switch (value) {
        case err_redis_ok:
            return "No error";
        case err_redis_io:
            return "I/O error";
        case err_redis_other:
            return "Other error";
        case err_redis_eof:
            return "Server closed the connection";
        case err_redis_protocol:
            return "Protocol parse error";
        case err_redis_oom:
            return "Out of memory";
        case err_redis_not_estab:
            return "Not connected";
        case err_redis_invalid_request:
            return "Invalid request";
        case err_redis_command_error:
            return "Command runtime error";
        default:
            return "Unkown redis error";
        }
    }
};

std::error_code MakeRedisErrorCode(int code);

class RedisPipeline
{
public:
    typedef std::vector<std::string> request_t;
    typedef std::vector<std::string> reply_t;

    ~RedisPipeline();
    bool Connect(std::string const& host, int16_t port, int timeout_ms = 10000);
    reply_t Call(request_t const& request, std::error_code * ec = nullptr);
    reply_t Call(std::string const& command, std::error_code * ec = nullptr);

private:
    bool Convert(reply_t & rt, redisReply *reply);
    void ReadLoop();
    void WriteLoop();

private:
    bool establish_ = false;
    bool writing_ = false;
    co_chan<void> write_condition_{1};
    redisContext *ctx_ = nullptr;
    co_mutex pipeline_mutex_;
    std::list<co_chan<std::pair<reply_t, std::error_code>>> pipeline_;
    co_chan<void> shutdown_c_{2};
    co_chan<void> exit_c_;
};
