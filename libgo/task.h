#pragma once
#include <libgo/config.h>
#include <libgo/context.h>
#include <libgo/ts_queue.h>
#include <libgo/timer.h>
#include <string.h>
#include <libgo/util.h>
#include "fd_context.h"

namespace co
{

enum class TaskState
{
    init,
    runnable,
    io_block,       // write, writev, read, select, poll, ...
    sys_block,      // co_mutex, ...
    sleep,          // sleep, nanosleep, poll(NULL, 0, timeout)
    done,
    fatal,
};

std::string GetTaskStateName(TaskState state);

typedef std::function<void()> TaskF;

class BlockObject;
class Processer;

class __CoLocalHolder {
public:
	inline __CoLocalHolder() = default;
	virtual ~__CoLocalHolder() = default;

private:
	__CoLocalHolder(const __CoLocalHolder &) = delete;
	__CoLocalHolder(__CoLocalHolder &&) = delete;
	__CoLocalHolder& operator=(const __CoLocalHolder &) = delete;
	__CoLocalHolder& operator=(__CoLocalHolder &&) = delete;
};

struct Task
    : public TSQueueHook, public RefObject
{
    uint64_t id_;
    TaskState state_ = TaskState::init;
    uint64_t yield_count_ = 0;
    Processer* proc_ = NULL;
    Context ctx_;
    std::string debug_info_;
    TaskF fn_;
    SourceLocation location_;
    std::exception_ptr eptr_;           // 保存exception的指针

    // Network IO block所需的数据
    // shared_ptr不具有线程安全性, 只能在协程中和SchedulerSwitch中使用.
    IoSentryPtr io_sentry_;     

    BlockObject* block_ = nullptr;      // sys_block等待的block对象
    uint32_t block_sequence_ = 0;       // sys_block等待序号(用于做超时校验)
    CoTimerPtr block_timer_;         // sys_block带超时等待所用的timer
	MininumTimeDurationType block_timeout_{ 0 }; // sys_block超时时间
    bool is_block_timeout_ = false;     // sys_block的等待是否超时

	int sleep_ms_ = 0;                  // 睡眠时间

	std::map<unsigned int, std::unique_ptr<__CoLocalHolder>> co_local_map_; //保存协程本地变量

    explicit Task(TaskF const& fn, std::size_t stack_size,
            const char* file, int lineno);
    ~Task();

    void InitLocation(const char* file, int lineno);

    ALWAYS_INLINE bool SwapIn()
    {
        return ctx_.SwapIn();
    }
    ALWAYS_INLINE bool SwapOut()
    {
        return ctx_.SwapOut();
    }

    void SetDebugInfo(std::string const& info);
    const char* DebugInfo();

    void Task_CB();

    static atomic_t<uint64_t> s_id;
    static atomic_t<uint64_t> s_task_count;

	static Task* GetCurrentTask();
    static uint64_t GetTaskCount();

    static LFLock s_stat_lock;
    static std::set<Task*> s_stat_set;
    static std::map<SourceLocation, uint32_t> GetStatInfo();
    static std::vector<std::map<SourceLocation, uint32_t>> GetStateInfo();
};

//协程本地变量
template<class T>
class CoLocal final {
private:
	class __HolderImpl: public __CoLocalHolder {
	public:
		inline __HolderImpl() = default;
		inline T* Get() const {
			return p_obj_;
		}
		template<class ... Args>
		T* Emplace(Args&&... args) {
			Release();
			ObjData* p = (ObjData*) &data_;
			new (p) ObjData(std::forward<Args>(args)...);
			this->p_obj_ = &p->obj_;
			return Get();
		}
		T* Set(const T& obj) {
			if (p_obj_) {
				*p_obj_ = obj;
			} else {
				emplace(obj);
			}
			return Get();
		}
		T* Set(T&& obj) {
			if (p_obj_) {
				*p_obj_ = obj;
			} else {
				emplace(obj);
			}
			return Get();
		}
		void Release() {
			if (p_obj_) {
				((ObjData*) &data_)->~ObjData();
				this->p_obj_ = nullptr;
			}
		}
		virtual ~__HolderImpl() {
			Release();
		}

	private:
		struct ObjData {
			T obj_;

			template<class ... Args>
			inline ObjData(Args&&... args) :
					obj_(std::forward<Args>(args)...) {
			}
		};

		char data_[sizeof(ObjData)];

		T* p_obj_ = nullptr;
	};

public:
	inline explicit CoLocal(unsigned int var_id) :
			var_id_(var_id) {
	}
	template<class ... Args>
	inline T* Emplace(Args&&... args) {
		return __GetHolder()->Emplace(std::forward<Args>(args)...);
	}
	inline T* Set(const T& obj) {
		return __GetHolder()->Set(obj);
	}
	inline T* Set(T&& obj) {
		return __GetHolder()->Set(obj);
	}
	inline T* Get() const {
		return __GetHolder()->Get();
	}
	inline operator bool() const {
		return !!Get();
	}
	inline T* operator ->() const {
		return Get();
	}
	inline T* operator *() const {
		return Get();
	}
	void Release() {
		Task* task = Task::GetCurrentTask();
		if (task) {
			auto& map = task->co_local_map_;
			auto it = map.find(var_id_);
			if (it != map.end()) {
				map.erase(it);
			}
		}
	}

private:
	const unsigned int var_id_;

private:
	CoLocal(const CoLocal &) = delete;
	CoLocal(CoLocal &&) = delete;
	CoLocal& operator=(const CoLocal &) = delete;
	CoLocal& operator=(CoLocal &&) = delete;

	std::unique_ptr<__HolderImpl >& __GetHolder() const {
		Task* task = Task::GetCurrentTask();
		if (!task) {
			ThrowError(eCoErrorCode::ec_use_co_local_outside_of_coroutine);
		}
		std::unique_ptr<__CoLocalHolder>* holder;
		auto& map = task->co_local_map_;
		auto it = map.find(var_id_);
		if (it == map.end()) {
			holder = &map[var_id_];
			holder->reset(new __HolderImpl);
		} else {
			holder = &it->second;
		}
		return *(std::unique_ptr<__HolderImpl>*) holder;
	}
};

template<class T>
using co_local = CoLocal<T>;

} //namespace co
