#include "scheduler.h"
#include "../common/error.h"
#include "../common/clock.h"
#include <stdio.h>
#include <system_error>
#include <unistd.h>
#include <time.h>
#include "ref.h"
#include <thread>

namespace co
{

inline atomic_t<unsigned long long> & GetTaskIdFactory()
{
    static atomic_t<unsigned long long> factory;
    return factory;
}

std::mutex& ExitListMtx()
{
    static std::mutex mtx;
    return mtx;
}
std::vector<std::function<void()>>* ExitList()
{
    static std::vector<std::function<void()>> *vec = new std::vector<std::function<void()>>;
    return vec;
}

static void onExit(void) {
    auto vec = ExitList();
    for (auto fn : *vec) {
        fn();
    }
    vec->clear();
//    return 0;
}

static int InitOnExit() {
    atexit(&onExit);
    return 0;
}

bool& Scheduler::IsExiting() {
    static bool exiting = false;
    return exiting;
}

Scheduler* Scheduler::Create()
{
    static int ignore = InitOnExit();
    (void)ignore;

    Scheduler* sched = new Scheduler;
    std::unique_lock<std::mutex> lock(ExitListMtx());
    auto vec = ExitList();
    vec->push_back([=] { delete sched; });
    return sched;
}

Scheduler::Scheduler()
{
    LibgoInitialize();
    processers_.push_back(new Processer(this, 0));
}

Scheduler::~Scheduler()
{
    IsExiting() = true;
    Stop();
}

void Scheduler::CreateTask(TaskF const& fn, TaskOpt const& opt)
{
    Task* tk = new Task(fn, opt.stack_size_ ? opt.stack_size_ : CoroutineOptions::getInstance().stack_size);
//    printf("new tk = %p  impl = %p\n", tk, tk->impl_);
    tk->SetDeleter(Deleter(&Scheduler::DeleteTask, this));
    tk->id_ = ++GetTaskIdFactory();
    TaskRefAffinity(tk) = opt.affinity_;
    TaskRefLocation(tk).Init(opt.file_, opt.lineno_);
    ++taskCount_;

    DebugPrint(dbg_task, "task(%s) created in scheduler(%p).", TaskDebugInfo(tk), (void*)this);
#if ENABLE_DEBUGGER
    if (Listener::GetTaskListener()) {
        Listener::GetTaskListener()->onCreated(tk->id_);
    }
#endif

    AddTask(tk);
}

void Scheduler::DeleteTask(RefObject* tk, void* arg)
{
    Scheduler* self = (Scheduler*)arg;
    delete tk;
    --self->taskCount_;
}

bool Scheduler::IsCoroutine()
{
    return !!Processer::GetCurrentTask();
}

bool Scheduler::IsEmpty()
{
    return taskCount_ == 0;
}

void Scheduler::Start(int minThreadNumber, int maxThreadNumber)
{
    if (!started_.try_lock())
        throw std::logic_error("libgo repeated call Scheduler::Start");

    if (minThreadNumber < 1)
       minThreadNumber = std::thread::hardware_concurrency();

    if (maxThreadNumber == 0 || maxThreadNumber < minThreadNumber)
        maxThreadNumber = minThreadNumber;

    minThreadNumber_ = minThreadNumber;
    maxThreadNumber_ = maxThreadNumber;

    auto mainProc = processers_[0];

    for (int i = 0; i < minThreadNumber_ - 1; i++) {
        NewProcessThread();
    }

    // 唤醒协程的定时器线程
    if (timer_) {
        timer_->SetPoolSize(1000, 100);
        std::thread t([this]{ 
                DebugPrint(dbg_thread, "Start alone timer(sched=%p) thread id: %lu", (void*)this, NativeThreadID());
                this->timer_->ThreadRun(); 
            });
        timerThread_.swap(t);
    }

    // 调度线程
    if (maxThreadNumber_ > 1) {
        DebugPrint(dbg_scheduler, "---> Create DispatcherThread");
        std::thread t([this]{
                DebugPrint(dbg_thread, "Start dispatcher(sched=%p) thread id: %lu", (void*)this, NativeThreadID());
                this->DispatcherThread();
                });
        dispatchThread_.swap(t);
    } else {
        DebugPrint(dbg_scheduler, "---> No DispatcherThread");
    }

    std::thread(FastSteadyClock::ThreadRun).detach();

    DebugPrint(dbg_scheduler, "Scheduler::Start minThreadNumber_=%d, maxThreadNumber_=%d", minThreadNumber_, maxThreadNumber_);
    mainProc->Process();
}
void Scheduler::goStart(int minThreadNumber, int maxThreadNumber)
{
    std::thread([=]{ this->Start(minThreadNumber, maxThreadNumber); }).detach();
}
void Scheduler::Stop()
{
    std::unique_lock<std::mutex> lock(stopMtx_);

    if (stop_) return;

    stop_ = true;
    size_t n = processers_.size();
    for (size_t i = 0; i < n; ++i) {
        auto p = processers_[i];
        if (p)
            p->NotifyCondition();
    }

    if (timer_) timer_->Stop();

    if (dispatchThread_.joinable())
        dispatchThread_.join();

    if (timerThread_.joinable())
        timerThread_.join();
}
void Scheduler::UseAloneTimerThread()
{
    TimerType * timer = new TimerType;

    if (!started_.try_lock()) {
        timer->SetPoolSize(1000, 100);
        std::thread t([this, timer]{ 
                DebugPrint(dbg_thread, "Start alone timer(sched=%p) thread id: %lu", (void*)this, NativeThreadID());
                timer->ThreadRun(); 
                });
        timerThread_.swap(t);
    } else {
        started_.unlock();
    }

    std::atomic_thread_fence(std::memory_order_acq_rel);
    timer_ = timer;
}

static Scheduler::TimerType& staticGetTimer() {
    static Scheduler::TimerType *ptimer = new Scheduler::TimerType;
    std::thread *pt = new std::thread([=]{ 
            DebugPrint(dbg_thread, "Start global timer thread id: %lu", NativeThreadID());
            ptimer->ThreadRun();
            });
    std::unique_lock<std::mutex> lock(ExitListMtx());
    auto vec = ExitList();
    vec->push_back([=] {
            ptimer->Stop();
            if (pt->joinable())
                pt->join();
        });
    return *ptimer;
}

Scheduler::TimerType & Scheduler::StaticGetTimer() {
    static TimerType & timer = staticGetTimer();
    return timer;
}

void Scheduler::NewProcessThread()
{
    auto p = new Processer(this, processers_.size());
    DebugPrint(dbg_scheduler, "---> Create Processer(%d)", p->id_);
    std::thread t([this, p]{
            DebugPrint(dbg_thread, "Start process(sched=%p) thread id: %lu", (void*)this, NativeThreadID());
            p->Process();
            });
    t.detach();
    processers_.push_back(p);
}

void Scheduler::DispatchBlocks(Scheduler::BlockMap &blockings,Scheduler::ActiveMap &actives)
{
   if(blockings.size() == 0)
      return;
   //将阻塞p的协程都steal出来
   SList<Task> tasks;
   for (auto &kv : blockings) {
        auto p = processers_[kv.first];
        tasks.append(p->Steal(0));
    }
    
    if(tasks.empty())
       return;
   
    ActiveMap newActives;
    //总协程数
    std::size_t totalTasks = tasks.size();
    //需要平分协程p的数量
    std::size_t LowerNum = 0;
    //平分的协程数
    std::size_t avg = 0;    
    
    auto LowerP = actives.begin();

    for( ; LowerP != actives.end(); ++LowerP)
    {
         totalTasks += LowerP->first;
         ++LowerNum;
         avg = totalTasks / LowerNum;
         //p的负载大于avg则将它移除
         if(LowerP->first > avg)
         {
             totalTasks -= LowerP->first;
             --LowerNum;
             avg = totalTasks / LowerNum;
             break;
         }        
    }

    if(LowerP != actives.end())
       ++LowerP;
    
    for(auto it = actives.begin(); it != LowerP; ++it)
    {
        SList<Task> in = tasks.cut(avg - it->first);
        
        if(in.empty())
          break;

        auto p = processers_[it->second];

        p->AddTask(std::move(in));
    }
    //还剩下task就全都给最小的p
    if(!tasks.empty())
    {
        auto p = processers_[actives.begin()->second];
        p->AddTask(std::move(tasks));
    }

    
}
void Scheduler::LoadBalance(Scheduler::ActiveMap &actives,std::size_t activeTasks)
{
    
     std::size_t avg = activeTasks / actives.size();
     
     if(actives.begin()->first > avg * CoroutineOptions::getInstance().load_balance_rate)
        return;
     
     SList<Task> tasks;
     for(auto it = actives.rbegin(); it != actives.rend(); ++it)
     {
          
          if(it->first <= avg)
             break;

          auto p = processers_[it->second];

          SList<Task> in = p->Steal(it->first - avg); 

          tasks.append(std::move(in));
     }

     if(tasks.empty())
        return;
     
     for(auto &kv : actives)
     {
         if(kv.first >= avg)
            break;
         auto p = processers_[kv.second];

         auto in = tasks.cut(avg - kv.first);
             
         p->AddTask(std::move(in));
     }
     //如果还剩下task,全都给最小的p
     if(!tasks.empty())
     {
         auto p = processers_[actives.begin()->second];
         p->AddTask(std::move(tasks));
     }
}
void Scheduler::DispatcherThread()
{
    DebugPrint(dbg_scheduler, "---> Start DispatcherThread");
    while (!stop_) {
        // TODO: 用condition_variable降低cpu使用率
        std::this_thread::sleep_for(std::chrono::microseconds(CoroutineOptions::getInstance().dispatcher_thread_cycle_us));
 
        // 1.收集负载值, 收集阻塞状态, 打阻塞标记, 唤醒处于等待状态但是有任务的P
        idx_t pcount = processers_.size();
        std::size_t totalLoadaverage = 0;
        ActiveMap actives;
        BlockMap blockings;

        int isActiveCount = 0;
        for (std::size_t i = 0; i < pcount; i++) {
            auto p = processers_[i];
            //等待中的p不能算阻塞,无法加入新协程导致p饿死
            if (!p->IsWaiting() && p->IsBlocking()) {
                blockings[i] = p->RunnableSize();
                if (p->active_) {
                    p->active_ = false;
                    DebugPrint(dbg_scheduler, "Block processer(%d)", (int)i);
                }
            }
            
            if (p->active_)
                isActiveCount++;
        }
       

        // 还可激活几个P
        int activeQuota = isActiveCount < minThreadNumber_ ? (minThreadNumber_ - isActiveCount) : 0;
        
        std::size_t activeTasks = 0;
        for (std::size_t i = 0; i < pcount; i++) {
            auto p = processers_[i];
            std::size_t loadaverage = p->RunnableSize();
            totalLoadaverage += loadaverage;

            if (!p->active_) {
                //处于等待中的p也应该唤醒
                if (activeQuota > 0 && (!p->IsBlocking() || p->IsWaiting())) {
                    p->active_ = true;
                    activeQuota--;
                    DebugPrint(dbg_scheduler, "Active processer(%d)", (int)i);
                    lastActive_ = i;
                }
            }

            if (p->active_) {
                actives.insert(ActiveMap::value_type{loadaverage, i});
                activeTasks += p->RunnableSize();
                p->Mark();
            }

            if (loadaverage > 0 && p->IsWaiting()) {
                p->NotifyCondition();
            }
        }

        if (actives.empty() && (int)pcount < maxThreadNumber_) {
            // 全部阻塞, 并且还有协程待执行, 起新线程
            NewProcessThread();
            actives.insert(ActiveMap::value_type{0, pcount});
            ++pcount;
        }

        
        // 全部阻塞并且不能起新线程, 无需调度, 等待即可
        if (actives.empty())
            continue;
        
        DispatchBlocks(blockings,actives);

        LoadBalance(actives,activeTasks);
       
    }
}

void Scheduler::AddTask(Task* tk)
{
    DebugPrint(dbg_scheduler, "Add task(%s) to runnable list.", tk->DebugInfo());
    auto proc = tk->proc_;
    if (proc && proc->active_) {
        proc->AddTask(tk);
        return ;
    }

    proc = Processer::GetCurrentProcesser();
    if (proc && proc->active_ && proc->GetScheduler() == this) {
        proc->AddTask(tk);
        return ;
    }

    std::size_t pcount = processers_.size();
    std::size_t idx = lastActive_;
    for (std::size_t i = 0; i < pcount; ++i, ++idx) {
        idx = idx % pcount;
        proc = processers_[idx];
        if (proc && proc->active_)
            break;
    }
    proc->AddTask(tk);
}

uint32_t Scheduler::TaskCount()
{
    return taskCount_;
}

uint64_t Scheduler::GetCurrentTaskID()
{
    Task* tk = Processer::GetCurrentTask();
    return tk ? tk->id_ : 0;
}

uint64_t Scheduler::GetCurrentTaskYieldCount()
{
    Task* tk = Processer::GetCurrentTask();
    return tk ? tk->yieldCount_ : 0;
}

void Scheduler::SetCurrentTaskDebugInfo(std::string const& info)
{
    Task* tk = Processer::GetCurrentTask();
    if (!tk) return ;
    TaskRefDebugInfo(tk) = info;
}

} //namespace co
