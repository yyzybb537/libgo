/************************************************
 * libgo sample9
************************************************
 * 我们再来看一个有实用价值的例子：
 * 基于curl同步调用做的HTTP性能测试工具。
************************************************/
#include <chrono>
#include <boost/thread.hpp>
#include <curl/curl.h>
#include "coroutine.h"
#include "win_exit.h"

static std::atomic<int> g_ok{0}, g_error{0};

size_t curl_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return size * nmemb;
}

void request_once(const char* url, int i)
{
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_fwrite);
        for (;;) {
            CURLcode res = curl_easy_perform(curl);
            if (CURLE_OK == res)
                ++g_ok;
            else
                ++g_error;
        }

        curl_easy_cleanup(curl);
    } else {
        go [=]{ request_once(url, i); };
    }
}

void show_status()
{
    static int last_ok = 0, last_error = 0;
    printf("ok:%d, error:%d\n", g_ok - last_ok, g_error - last_error);
    last_ok = g_ok, last_error = g_error;
}

int main(int argc, char** argv)
{
    if (argc <= 2) {
        printf("Usage %s url concurrency\n", argv[0]);
        exit(1);
    }

    int concurrency = atoi(argv[2]);
    printf("start %d coroutines\n", concurrency);

    // 创建N个协程
    for (int i = 0; i < concurrency; ++i)
        go [=]{ request_once(argv[1], i); };

    go []{ 
        for (;;) {
            show_status(); 
            sleep(1);
        }
    };

    // 创建nCores个线程去并行执行所有协程
    co_sched.Start(0);
    return 0;
}

