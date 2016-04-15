/************************************************
 * 处理IO协程切换、epoll等待、等待成功、超时取消等待，
 *     及其多线程并行关系。
*************************************************
* 此图已过时！
*************************************************
 _______ *
    ^    * -------------------
    |    *       Syscall
    |    * -------------------
    |    *          |
    |    *          v
    |    * -------------------
Coroutine* save state/fds/timeout
runnable * -------------------
    |    *          |
    |    *          v
    |    * -------------------
    |    *        yield
    |    * -------------------
 ___v___ *          |
    ^    *          v
    |    * -------------------
    |    *  push to wait list
    |    * -------------------
    |    *          |
    |    *          v
    |    * -------------------
    |    *  add fd into epoll
    |    * -------------------__________________________________________________
    |    *          |             ^                                        ^
    |    *          .  <--------- | -----------------------------------    |
    |    *          .             |                 if failed, rollback    |
    |    *          .             |                 -------------------    
Scheduler*          v             |                          |         epoll_wait
io_block * -------------------    |                          v                 
    |    *  add fd into epoll     |                 -------------------    |
    |    * -------------------    |                 delete fds in epoll    |
    |    *          |                               -------------------____v____
    |    *          v         epoll_wait                     |
    |    * -------------------                               v
    |    *     set timer          |                 -------------------
    |    * -------------------___ | ______________  pop from wait list
    |    *          |             |          ^      -------------------
    |    *          v             |          |               |
    |    * -------------------    |          |               v
    |    *     begin wait         |      time out   -------------------
 ___v___ * -------------------    |          |      push into runnable list
    ^    *          |             |          |      -------------------
    |    *          .             |          |               |
    |    * epoll_wait             |          |               v
    |    *     or   .             |          |      -------------------
Wait Loop*  time out.             |          |       Once Syscall Done
io_block *          .             |          |      -------------------
    |    *          v         ____v____      |
    |    * -------------------               |
    |    * delete fds in epoll               |
    |    * -------------------               |
    |    *          |                        |
    |    *          v                        |
    |    * -------------------               |
    |    * push into runnable list           |
    |    * -------------------               |
 ___v___ *          |                        |
    ^    *          v                        |
    |    * -------------------               |
    |    * block cancel timer                |
    |    * -------------------_______________v____
Coroutine*          |
runnable *          v
    |    * -------------------
    |    *  Once Syscall Done
 ___v___ * -------------------
         *
*************************************************/
#pragma once
#include <vector>
#include <list>
#include <set>
#include "task.h"
#include "fd_context.h"

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
    int reactor_ctl(int epoll_ctl_mod, int fd, uint32_t poll_events, bool is_socket);
    // --------------------------------------

    int WaitLoop(int wait_milliseconds);

    bool IsEpollCreated() const;

private:
    void CreateEpoll();

    int epoll_fd_;
    LFLock epoll_create_lock_;
    int epoll_event_size_;
    pid_t epoll_owner_pid_;

    LFLock epoll_lock_;
    uint64_t loop_index_;

    typedef TSQueue<IoSentry> IoSentryList;
    IoSentryList wait_io_sentries_;


    // TODO: poll to support (file-fd, other-fd)
};

} //namespace co
