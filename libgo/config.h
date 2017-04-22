#pragma once
#include "cmake_config.h"
#include <unordered_map>
#include <list>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <atomic>
#include <mutex>
#include <assert.h>
#include <deque>
#include <string>
#include <type_traits>
#include <stddef.h>
#include <exception>
#include <vector>
#include <set>
#include <map>
#include <functional>
#include <chrono>
#include <memory>

// VS2013��֧��thread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
# define thread_local __declspec(thread)
# define UNSUPPORT_STEADY_TIME
#endif

#if defined(__GNUC__) && (__GNUC__ > 3 ||(__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
# define ALWAYS_INLINE __attribute__ ((always_inline))
#else
# define ALWAYS_INLINE inline
#endif

#if __linux__
#include <unistd.h>
#include <sys/types.h>
#endif

namespace co
{

#if LIBGO_SINGLE_THREAD
	template <typename T>
	using atomic_t = T;
#else
	template <typename T>
	using atomic_t = std::atomic<T>;
#endif

///---- debugger flags
	static const uint64_t dbg_none              = 0;
	static const uint64_t dbg_all               = ~(uint64_t)0;
	static const uint64_t dbg_hook              = 0x1;
	static const uint64_t dbg_yield             = 0x1 << 1;
	static const uint64_t dbg_scheduler         = 0x1 << 2;
	static const uint64_t dbg_task              = 0x1 << 3;
	static const uint64_t dbg_switch            = 0x1 << 4;
	static const uint64_t dbg_ioblock           = 0x1 << 5;
	static const uint64_t dbg_wait              = 0x1 << 6;
	static const uint64_t dbg_exception         = 0x1 << 7;
	static const uint64_t dbg_syncblock         = 0x1 << 8;
	static const uint64_t dbg_timer             = 0x1 << 9;
	static const uint64_t dbg_scheduler_sleep   = 0x1 << 10;
	static const uint64_t dbg_sleepblock        = 0x1 << 11;
	static const uint64_t dbg_spinlock          = 0x1 << 12;
	static const uint64_t dbg_fd_ctx            = 0x1 << 13;
	static const uint64_t dbg_debugger          = 0x1 << 14;
	static const uint64_t dbg_signal            = 0x1 << 15;
	static const uint64_t dbg_sys_max           = dbg_debugger;
///-------------------

#if __linux__
	typedef std::chrono::nanoseconds MininumTimeDurationType;
#else
	typedef std::chrono::microseconds MininumTimeDurationType;
#endif

// ��Э�̷��ɵ��߳��еĲ���
	enum e_go_dispatch
	{
		egod_default = -4,  // if enable_work_steal, it's equal egod_local_thread; else, equal egod_robin.
		egod_random = -3,
		egod_robin = -2,
		egod_local_thread = -1,

		// ...
	};

// Э�����׳�δ�����쳣ʱ�Ĵ���ʽ
	enum class eCoExHandle : uint8_t
	{
		immedaitely_throw,  // �����׳�
		delay_rethrow,      // �ӳٵ�����������ʱ�׳�
		debugger_only,      // ����ӡ������Ϣ
	};

	typedef void*(*stack_malloc_fn_t)(size_t size);
	typedef void(*stack_free_fn_t)(void *ptr);

///---- ����ѡ��
	struct CoroutineOptions
	{
		/*********************** Debug options **********************/
		// ����ѡ��, ����: dbg_switch �� dbg_hook|dbg_task|dbg_wait
		uint64_t debug = 0;

		// ������Ϣ���λ�ã���д�������������ض������λ��
		FILE* debug_output = stdout;
		/************************************************************/

		/**************** Stack and Exception options ***************/
		// Э�����׳�δ�����쳣ʱ�Ĵ���ʽ
		eCoExHandle exception_handle = eCoExHandle::immedaitely_throw;

		// Э��ջ��С����, ֻ��Ӱ���ڴ�ֵ����֮���´�����P, �������״�Runǰ����.
		// stack_size�������ò�����1MB
		// Linuxϵͳ��, ����2MB��stack_size�ᵼ���ύ�ڴ��ʹ������1MB��stack_size��10��.
		uint32_t stack_size = 1 * 1024 * 1024;

		// û��Э����Ҫ����ʱ, Run������ߵĺ�����(������ʵʱ��ϵͳ���Կ��ǵ������ֵ)
		uint8_t max_sleep_ms = 20;

		// ÿ����ʱ��ÿ֡�������������(Ϊ0��ʾ����, ÿ֡����ǰ���п��Դ��������)
		uint32_t timer_handle_every_cycle = 0;

		// epollÿ�δ�����event����(Windows����Ч)
		uint32_t epoll_event_size = 10240;

		// �Ƿ�����worksteal�㷨
		bool enable_work_steal = true;

		// �Ƿ�����Э��ͳ�ƹ���(����һ���������, Ĭ�ϲ�����)
		bool enable_coro_stat = false;

		// ջ�����ñ����ڴ�ε��ڴ�ҳ����(��linux����Ч)(Ĭ��Ϊ0, ��:������)
		// ��ջ���ڴ������ǰ��ҳ����Ϊprotect����.
		// ���Կ�����ѡ��ʱ, stack_size��������protect_stack_page+1ҳ
		uint32_t & protect_stack_page;

		// ����ջ�ڴ����(malloc/free)
		// ʹ��fiber��Э�̵ײ�ʱ��Ч
		stack_malloc_fn_t & stack_malloc_fn;
		stack_free_fn_t & stack_free_fn;

		CoroutineOptions();

		inline static CoroutineOptions& getInstance()
		{
			static CoroutineOptions obj;
			return obj;
		}
	};

	extern uint32_t codebug_GetCurrentProcessID();
	extern uint32_t codebug_GetCurrentThreadID();
	extern std::string codebug_GetCurrentTime();
	extern const char* BaseFile(const char* file);

} //namespace co

#define DebugPrint(type, fmt, ...) \
    do { \
        if (::co::CoroutineOptions::getInstance().debug & (type)) { \
            fprintf(::co::CoroutineOptions::getInstance().debug_output, "[%s][%05u][%02u]%s:%d:(%s)\t " fmt "\n", \
                    ::co::codebug_GetCurrentTime().c_str(),\
                    ::co::codebug_GetCurrentProcessID(), ::co::codebug_GetCurrentThreadID(), \
                    ::co::BaseFile(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            fflush(::co::CoroutineOptions::getInstance().debug_output); \
        } \
    } while(0)

