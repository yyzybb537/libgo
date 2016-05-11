/***********************************************
 * libgo sample4
************************************************
 * libgo支持异常安全, 对于协程中的抛出未捕获
 * 的异常提供以下几种处理方式:
 *   1.立即在协程栈上抛出异常, 此举会导致进程直接崩溃, 但是可以生成带有堆栈的coredump
 *     设置方法：
 *       co_sched.GetOptions().exception_handle = co::eCoExHandle::immedaitely_throw;
 *   2.结束当前协程, 使其堆栈回滚, 将异常暂存至调度器Run时抛出.
 *     设置方法：
 *       co_sched.GetOptions().exception_handle = co::eCoExHandle::delay_rethrow;
 *   3.结束当前协程, 吃掉异常, 仅打印一些日志信息.
 *     设置方法：
 *       co_sched.GetOptions().exception_handle = co::eCoExHandle::debugger_only;
 *
 * 显示日志信息需要打开exception相关的调试信息:
 *       co_sched.GetOptions().debug |= dbg_exception;
 *
 * 日志信息默认显示在标准输出上，允许用户重定向
************************************************/
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "coroutine.h"
#include "win_exit.h"

int main()
{
    co_sched.GetOptions().exception_handle = co::eCoExHandle::delay_rethrow;
    go []{ throw 1; };
    try {
        co_sched.RunUntilNoTask();
    } catch (int v) {
        printf("caught delay throw exception:%d\n", v);
    }

    co_sched.GetOptions().debug |= co::dbg_exception;
    co_sched.GetOptions().exception_handle = co::eCoExHandle::debugger_only;
    go []{
        // 为了使打印的日志信息更加容易辨识，还可以给当前协程附加一些调试信息。
        co_sched.SetCurrentTaskDebugInfo("throw_ex");

        // 重定向日志信息输出至文件
        co_sched.GetOptions().debug_output = fopen("log", "a+");

        throw std::exception();
    };
    co_sched.RunUntilNoTask();
    return 0;
}

