/************************************************
 * 处理IO协程切换、epoll等待、等待成功、超时取消等待，
 *     及其多线程并行关系。
*************************************************/
#pragma once
#include <vector>
#include <list>
#include <set>
#include <libgo/task.h>
#include "fd_context.h"
#include <libgo/debugger.h>

namespace co
{

class IoWait
{
public:
    IoWait();

    int GetEpollFd();

    // 在协程中调用的switch, 暂存状态并yield
    void CoSwitch();

    // --------------------------------------
    /*
    * 以下两个接口实现了ABBA式的并行
    */
    // 在调度器中调用的switch, 如果成功则进入等待队列，如果失败则重新加回runnable队列
    void SchedulerSwitch(Task* tk);

    // trigger by timer or epoll or poll.
    void IOBlockTriggered(IoSentryPtr io_sentry);
    void __IOBlockTriggered(IoSentryPtr io_sentry);
    // --------------------------------------

    // --------------------------------------
    /*
    * reactor相关操作, 使用类似epoll的接口屏蔽epoll/poll的区别
    * TODO: 同时支持socket-io和文件io.
    */
    int reactor_ctl(int epollfd, int epoll_ctl_mod, int fd,
            uint32_t poll_events, bool is_support, bool et_mode);
    // --------------------------------------

    int WaitLoop(int wait_milliseconds);

    bool IsEpollCreated();

private:
    void CreateEpoll();

    void IgnoreSigPipe();

    int& EpollFdRef();
    pid_t& EpollOwnerPid();

    LFLock epoll_create_lock_;
    int epoll_event_size_;

    typedef TSQueue<IoSentry> IoSentryList;
    IoSentryList wait_io_sentries_;

    friend class CoDebugger;

    // TODO: poll to support (file-fd, other-fd)
};

} //namespace co
