/************************************************
 * libgo sample12
 ************************************************
 * 这是一个协程监听器的例子
 * 使用协程监听器 co_listener 可以监听协程的
 * 创建销毁等事件，也可以进行异常处理
 ************************************************/
#include "coroutine.h"
#include <iostream>

using namespace std;

//协程监听器的调用过程：
// s: Scheduler，表示该方法运行在调度器上下文中
// c: Coroutine，表示该方法运行在协程上下文中
//                                                          (正常运行完成)
//                                                       -->[c]onCompleted->
//                                                      |                   |
// [s]onInit-->[s]onCreated-->[s]onSwapIn-->[c]onStart->*--->[c]onSwapOut-- -->[c]onFinished-->[c]onSwapOut
//                                                      |\                | |
//                                                      | \<-[s]onSwapIn--V |
//                                                      |                   |
//                                                       -->[c]onException->
//                                                      (运行时抛出未捕获的异常)
//
//！！注意协程监听器回调方法均不能抛出异常，如果可能有异常抛出，请在回调方法内自行 try...catch 消化掉

//覆盖 co::co_listener 的虚函数实现回调方法
class CoListenerSample: public co::Listener::TaskListener {
public:
    /**
     * 协程准备初始化、即将被创建的时候被调用，可以进行对协程的任务进行封装或者拦截
     * （注意此时并未运行在协程中）
     *
     * @prarm task_id 协程ID
     * @prarm fn 协程任务，可以赋值修改此参数对协程任务进行二次封装
     * @param opt 协程创建的参数，可以赋值修改此参数值
     *
     * @return 返回true，正常创建该任务；返回false，放弃此任务
     */
    virtual bool onInit(uint64_t task_id, co::TaskF& fn, co::TaskOpt& opt) noexcept {
        cout << "onInit task_id=" << task_id << endl;
        fn = [fn]() {
            cout << "haha, I'm coming.  " << endl;
            fn();
        };
        return true;
    }

    /**
     * 协程被创建时被调用
     * （注意此时并未运行在协程中）
     *
     * @prarm task_id 协程ID
     * @prarm eptr
     */
    virtual void onCreated(uint64_t task_id) noexcept {
        cout << "onCreated task_id=" << task_id << endl;
    }

    /**
     * 每次协程切入前调用
     * （注意此时并未运行在协程中）
     *
     * @prarm task_id 协程ID
     * @prarm eptr
     */
    virtual void onSwapIn(uint64_t task_id) noexcept {
        cout << "onSwapIn task_id=" << task_id << endl;
    }
    
    /**
     * 协程开始运行
     * （本方法运行在协程中）
     *
     * @prarm task_id 协程ID
     * @prarm eptr
     */
    virtual void onStart(uint64_t task_id) noexcept {
        cout << "onStart task_id=" << task_id << endl;
    }

    /**
     * 每次协程切出前调用
     * （本方法运行在协程中）
     *
     * @prarm task_id 协程ID
     * @prarm eptr
     */
    virtual void onSwapOut(uint64_t task_id) noexcept {
        cout << "onSwapOut task_id=" << task_id << endl;
    }

    /**
     * 协程正常运行结束（无异常抛出）
     * （本方法运行在协程中）
     *
     * @prarm task_id 协程ID
     */
    virtual void onCompleted(uint64_t task_id) noexcept {
        cout << "onCompleted task_id=" << task_id << endl;
    }

    /**
     * 协程抛出未被捕获的异常（本方法运行在协程中）
     * @prarm task_id 协程ID
     * @prarm eptr 抛出的异常对象指针
     *             ！！注意：当 exception_handle 配置为 immedaitely_throw 时本事件
     *             ！！与 onFinished() 均失效，异常发生时将直接抛出并中断程序的运行，同时生成coredump
     */
    virtual void onException(uint64_t task_id, std::exception_ptr& eptr) noexcept {
        cout << "onException task_id=" << task_id << "  ";

        try {
            rethrow_exception(eptr);
        } catch (exception& e) {
            cout << "onException  e=" << e.what() << endl;
        } catch (...) {
            cout << "unknow exception." << endl;
        }

        eptr = nullptr;
    }

    /**
     * 协程运行完成，if(eptr) 为false说明协程正常结束，为true说明协程抛出了了异常
     *
     * @prarm task_id 协程ID
     * @prarm eptr 抛出的异常对象指针，此处只能读取不允许修改其值
     */
    virtual void onFinished(uint64_t task_id, const std::exception_ptr eptr) noexcept {
        cout << "onFinished task_id=" << task_id << "  ";
        if (eptr) {
            try {
                rethrow_exception(eptr);
            } catch (exception& e) {
                cout << "e=" << e.what() << endl;
            } catch (...) {
                cout << "unknow exception." << endl;
            }
        } else {
            cout << " completed." << endl;
        }
    }

};

int main(int argc, char** argv) {
#if ENABLE_LISTENER
    CoListenerSample listener;

    //设置协程监听器，如果设置为NULL则为取消监听
    co::Listener::SetTaskListener(&listener);

    //将异常的处理方式置为使用listener处理
    co_opt.exception_handle = co::eCoExHandle::on_listener;

    go[] {
        cout << "i am task=" << co_sched.GetCurrentTaskID() << endl;
        cout << "task " << co_sched.GetCurrentTaskID() << " going to sleep for a while" << endl;
        co_sleep(1);
        cout << "task " << co_sched.GetCurrentTaskID() << " returns" << endl;
    };

    go[] {
        cout << "i am task=" << co_sched.GetCurrentTaskID() << endl;
        cout << "task " << co_sched.GetCurrentTaskID() << " going to sleep for a while" << endl;
        co_sleep(1);
        throw logic_error("wtf!!??");
        cout << "task " << co_sched.GetCurrentTaskID() << " returns" << endl;
    };

    // 200ms后安全退出
    std::thread([]{ co_sleep(200); co_sched.Stop(); }).detach();

    co_sched.Start();
#endif
    return 0;
}

