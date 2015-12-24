#include <hiredis/hiredis.h>
#include "coroutine.h"
#include <stdio.h>
#include <memory>
#include <strings.h>

void do_redis(int num)
{
    redisContext* redis_ctx = redisConnect("127.0.0.1", 6379);
    if (!redis_ctx) {
        printf("[%d] connect error.\n", num);
        return ;
    }

    if (redis_ctx->err) {
        printf("[%d] connect error %d\n", num, redis_ctx->err);
        return ;
    }

    printf("[%d] connected redis.\n", num);
    std::shared_ptr<redisContext> _ep(redis_ctx, [](redisContext* c){ redisFree(c); });

    const char* cmd_set = "set i 1";
    redisReply *reply = (redisReply*)redisCommand(redis_ctx, cmd_set);
    if (!reply) {
        printf("[%d] reply is NULL.\n", num);
        return ;
    }

//    printf("[%d] got reply.\n", num);
    std::shared_ptr<redisReply> _ep_reply(reply, [](redisReply* reply){ freeReplyObject(reply); });

    if (!(reply->type == REDIS_REPLY_STATUS && strcasecmp(reply->str,"OK")==0)) {
        printf("[%d] execute command error.\n", num);
        return;  
    }     

    printf("[%d] execute command success.\n", num);
}

int main()
{
//    g_Scheduler.GetOptions().debug = dbg_all;
    for (int i = 0; i < 2; ++i)
    {
        go [=]{ do_redis(i); };
    }
    printf("go\n");
    while (!g_Scheduler.IsEmpty())
        g_Scheduler.Run();
    printf("end\n");
    return 0;
}
