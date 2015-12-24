/************************************************
 * 处理IO协程切换、epoll等待、等待成功、超时取消等待，
 *     及其多线程并行关系。
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

namespace co
{

class IoWait
{
public:
    IoWait();

    // 在协程中调用的switch, 暂存状态并yield
    void CoSwitch(std::vector<FdStruct> && fdsts, int timeout_ms);

    // 在调度器中调用的switch, 如果成功则进入等待队列，如果失败则重新加回runnable队列
    void SchedulerSwitch(Task* tk);

    int WaitLoop();

private:
    void Cancel(Task *tk, uint32_t id);

    int ChooseEpoll(uint32_t event);

    struct EpollWaitSt
    {
        Task* tk;
        uint32_t id;

        friend bool operator<(EpollWaitSt const& lhs, EpollWaitSt const& rhs) {
            return lhs.tk < rhs.tk;
        }
    };

    int epoll_fds_[2];
    LFLock epoll_lock_;
    std::set<EpollWaitSt> epollwait_tasks_;
    std::list<CoTimerPtr> timeout_list_;
    LFLock timeout_list_lock_;
    CoTimerMgr timer_mgr_;

    typedef TSQueue<Task> TaskList;
    TaskList wait_tasks_;
};


} //namespace co
