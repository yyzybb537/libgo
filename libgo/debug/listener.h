#pragma once
#include <exception>

namespace co
{

class Listener
{
public:
    /**
     * 协程事件监听器
     * 注意：其中所有的回调方法都不允许抛出异常
     */
    class TaskListener {
    public:
        /**
         * 协程被创建时被调用
         * （注意此时并未运行在协程中）
         *
         * @prarm task_id 协程ID
         * @prarm eptr
         */
        virtual void onCreated(uint64_t task_id) noexcept {
        }
        
        /**
         * 每次协程切入前调用
         * （注意此时并未运行在协程中）
         *
         * @prarm task_id 协程ID
         * @prarm eptr
         */
        virtual void onSwapIn(uint64_t task_id) noexcept {
        }

        /**
         * 协程开始运行
         * （本方法运行在协程中）
         *
         * @prarm task_id 协程ID
         * @prarm eptr
         */
        virtual void onStart(uint64_t task_id) noexcept {
        }

        /**
         * 每次协程切出前调用
         * （本方法运行在协程中）
         *
         * @prarm task_id 协程ID
         * @prarm eptr
         */
        virtual void onSwapOut(uint64_t task_id) noexcept {
        }
        
        /**
         * 协程正常运行结束（无异常抛出）
         * （本方法运行在协程中）
         *
         * @prarm task_id 协程ID
         */
        virtual void onCompleted(uint64_t task_id) noexcept {
        }

        /**
         * 协程抛出未被捕获的异常（本方法运行在协程中）
         * @prarm task_id 协程ID
         * @prarm eptr 抛出的异常对象指针，可对本指针赋值以修改异常对象，
         *             异常将使用 CoroutineOptions.exception_handle 中
         *             配置的方式处理；赋值为nullptr则表示忽略此异常
         *             ！！注意：当 exception_handle 配置为 immedaitely_throw 时本事件
         *             ！！与 onFinished() 均失效，异常发生时将直接抛出并中断程序的运行，同时生成coredump
         */
        virtual void onException(uint64_t task_id, std::exception_ptr& eptr) noexcept {
        }

        /**
         * 协程运行完成，if(eptr) 为false说明协程正常结束，为true说明协程抛出了了异常
         *（本方法运行在协程中）
         *
         * @prarm task_id 协程ID
         */
        virtual void onFinished(uint64_t task_id) noexcept {
        }

        virtual ~TaskListener() noexcept = default;

        // s: Scheduler，表示该方法运行在调度器上下文中
        // c: Coroutine，表示该方法运行在协程上下文中
        //
        //                                           -->[c]onCompleted->
        //                                          |                   |
        // [s]onCreated-->[s]onSwapIn-->[c]onStart->*--->[c]onSwapOut-- -->[c]onFinished-->[c]onSwapOut
        //                                          |\                | |
        //                                          | \<-[s]onSwapIn--V |
        //                                          |                   |
        //                                           -->[c]onException->
    };

public:
#if ENABLE_DEBUGGER
    ALWAYS_INLINE static TaskListener*& GetTaskListener() {
        static TaskListener* task_listener = nullptr;
        return task_listener;
    }

    static void SetTaskListener(TaskListener* listener) {
        GetTaskListener() = listener;
    }
#endif
};

} // namespace co
