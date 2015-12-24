#include "io_wait.h"
#include <sys/epoll.h>
#include "scheduler.h"

namespace co
{

enum class EpollType
{
    read,
    write,
};

IoWait::IoWait()
{
    for (int i = 0; i < 2; ++i)
    {
        epoll_fds_[i] = epoll_create(1024);
        if (epoll_fds_[i] != -1)
            continue;

        fprintf(stderr, "CoroutineScheduler init failed. epoll create error:%s\n", strerror(errno));
        exit(1);
    }
}

void IoWait::CoSwitch(std::vector<FdStruct> && fdsts, int timeout_ms)
{
    Task* tk = g_Scheduler.GetCurrentTask();
    if (!tk) return ;

    uint32_t id = ++tk->io_block_id_;
    tk->state_ = TaskState::io_block;
    tk->wait_successful_ = 0;
    tk->io_block_timeout_ = timeout_ms;
    tk->io_block_timer_.reset();
    tk->wait_fds_.swap(fdsts);
    for (auto &fdst : tk->wait_fds_) {
        fdst.epoll_ptr.tk = tk;
        fdst.epoll_ptr.io_block_id = id;
    }

    DebugPrint(dbg_ioblock, "task(%s) CoSwitch id=%d, nfds=%d, timeout=%d",
            tk->DebugInfo(), id, (int)fdsts.size(), timeout_ms);
    g_Scheduler.CoYield();
}

void IoWait::SchedulerSwitch(Task* tk)
{
    bool ok = false;
    std::unique_lock<LFLock> lock(tk->io_block_lock_, std::defer_lock);
    if (tk->wait_fds_.size() > 1)
        lock.lock();

    // id一定要先取出来, 因为在下面的for中, 有可能在另一个线程epoll_wait成功,
    // 并且重新进入一次syscall, 导致id变化.
    uint32_t id = tk->io_block_id_;

    RefGuard<> ref_guard(tk);
    wait_tasks_.push(tk);
    std::vector<std::pair<int, uint32_t>> rollback_list;
    for (auto &fdst : tk->wait_fds_)
    {
        epoll_event ev = {fdst.event, {(void*)&fdst.epoll_ptr}};
        int epoll_fd_ = ChooseEpoll(fdst.event);
        tk->IncrementRef();     // 先将引用计数加一, 以防另一个线程立刻epoll_wait成功被执行完线程.
        if (-1 == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fdst.fd, &ev)) {
            tk->DecrementRef(); // 添加失败时, 回退刚刚增加的引用计数.
            if (errno == EEXIST) {
                fprintf(stderr, "task(%s) add fd(%d) into epoll error %d:%s\n",
                        tk->DebugInfo(), fdst.fd, errno, strerror(errno));
                DebugPrint(dbg_ioblock, "task(%s) add fd(%d) into epoll error %d:%s\n",
                        tk->DebugInfo(), fdst.fd, errno, strerror(errno));
                // 某个fd添加失败, 回滚
                for (auto fd_pair : rollback_list)
                {
                    int epoll_fd_ = ChooseEpoll(fd_pair.second);
                    if (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd_pair.first, NULL)) {
                        DebugPrint(dbg_ioblock, "task(%s) rollback io_block. fd=%d",
                                tk->DebugInfo(), fd_pair.first);
                        // 减引用计数的条件：谁成功从epoll中删除了一个fd，谁才能减引用计数。
                        tk->DecrementRef();
                    }
                }
                ok = false;
                break;
            }

            // 其他原因添加失败, 忽略即可.(模拟poll逻辑)
            continue;
        }

        ok = true;
        rollback_list.push_back(std::make_pair(fdst.fd, fdst.event));
        DebugPrint(dbg_ioblock, "task(%s) io_block. fd=%d, ev=%d",
                tk->DebugInfo(), fdst.fd, fdst.event);
    }

    DebugPrint(dbg_ioblock, "task(%s) SchedulerSwitch id=%d, nfds=%d, timeout=%d, ok=%s",
            tk->DebugInfo(), id, (int)tk->wait_fds_.size(), tk->io_block_timeout_,
            ok ? "true" : "false");

    if (!ok) {
        if (wait_tasks_.erase(tk)) {
            g_Scheduler.AddTaskRunnable(tk);
        }
    }
    else if (tk->io_block_timeout_ != -1) {
        // set timer.
        tk->IncrementRef();
        uint64_t task_id = tk->id_;
        auto timer_id = timer_mgr_.ExpireAt(std::chrono::milliseconds(tk->io_block_timeout_),
                [=]{ 
                    DebugPrint(dbg_ioblock, "task(%d) syscall timeout", (int)task_id);
                    this->Cancel(tk, id);
                    tk->DecrementRef();
                });
        tk->io_block_timer_ = timer_id;
    }
}

void IoWait::Cancel(Task *tk, uint32_t id)
{
    DebugPrint(dbg_ioblock, "task(%s) Cancel id=%d, tk->io_block_id_=%d",
            tk->DebugInfo(), id, (int)tk->io_block_id_);

    if (tk->io_block_id_ != id)
        return ;

    if (wait_tasks_.erase(tk)) { // sync between timer and epoll_wait.
        DebugPrint(dbg_ioblock, "task(%s) io_block wakeup. id=%d", tk->DebugInfo(), id);

        std::unique_lock<LFLock> lock(tk->io_block_lock_, std::defer_lock);
        if (tk->wait_fds_.size() > 1)
            lock.lock();

        // 清理所有fd
        for (auto &fdst: tk->wait_fds_)
        {
            int epoll_fd_ = ChooseEpoll(fdst.event);
            if (0 == epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fdst.fd, NULL)) {   // sync 1
                DebugPrint(dbg_ioblock, "task(%s) io_block clear fd=%d", tk->DebugInfo(), fdst.fd);
                // 减引用计数的条件：谁成功从epoll中删除了一个fd，谁才能减引用计数。
                tk->DecrementRef(); // epoll use ref.
            }
        }

        g_Scheduler.AddTaskRunnable(tk);
    }
}

int IoWait::WaitLoop()
{
    int c = 0;
    for (;;) {
        std::list<CoTimerPtr> timers;
        timer_mgr_.GetExpired(timers, 128);
        if (timers.empty())
            break;

        c += timers.size();
        // 此处暂存callback而不是Task*，是为了block_cancel能够真实有效。
        std::unique_lock<LFLock> lock(timeout_list_lock_);
        timeout_list_.merge(std::move(timers));
    }

    std::unique_lock<LFLock> lock(epoll_lock_, std::defer_lock);
    if (!lock.try_lock())
        return c ? c : -1;

    static epoll_event evs[1024];
    int epoll_n = 0;
    for (int epoll_type = 0; epoll_type < 2; ++epoll_type)
    {
retry:
        int n = epoll_wait(epoll_fds_[epoll_type], evs, 1024, 0);
        if (n == -1 && errno == EAGAIN)
            goto retry;

        epoll_n += n;
        DebugPrint(dbg_scheduler, "do epoll(%d) event, n = %d", epoll_type, n);
        for (int i = 0; i < n; ++i)
        {
            EpollPtr* ep = (EpollPtr*)evs[i].data.ptr;
            ep->revent = evs[i].events;
            Task* tk = ep->tk;
            ++tk->wait_successful_;
            // 将tk暂存, 最后再执行Cancel, 是为了poll和select可以得到正确的计数。
            // 以防Task被加入runnable列表后，被其他线程执行
            epollwait_tasks_.insert(EpollWaitSt{tk, ep->io_block_id});
            DebugPrint(dbg_ioblock, "task(%s) epoll trigger io_block_id(%u)", tk->DebugInfo(), ep->io_block_id);
        }
    }

    for (auto &st : epollwait_tasks_)
        Cancel(st.tk, st.id);
    epollwait_tasks_.clear();

    std::list<CoTimerPtr> timeout_list;
    {
        std::unique_lock<LFLock> lock(timeout_list_lock_);
        timeout_list_.swap(timeout_list);
    }

    for (auto &cb : timeout_list)
        (*cb)();

    // 由于epoll_wait的结果中会残留一些未计数的Task*,
    //     epoll的性质决定了这些Task无法计数, 
    //     所以这个析构的操作一定要在epoll_lock的保护中做
    Task::DeleteList delete_list;
    Task::SwapDeleteList(delete_list);
    for (auto &tk : delete_list) {
        DebugPrint(dbg_task, "task(%s) delete.", tk->DebugInfo());
        delete tk;
    }

    return epoll_n + c;
}

int IoWait::ChooseEpoll(uint32_t event)
{
    return (event & EPOLLIN) ? epoll_fds_[(int)EpollType::read] : epoll_fds_[(int)EpollType::write];
}

} //namespace co
