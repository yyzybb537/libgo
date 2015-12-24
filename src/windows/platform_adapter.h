#pragma once

// VS2013不支持thread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
#define co_thread_local __declspec(thread)
#else 
#define co_thread_local thread_local
#endif

namespace co {

	struct ThreadLocalInfo;
	struct ProcesserRunGuard
	{
		ThreadLocalInfo *info_;
		ProcesserRunGuard(ThreadLocalInfo &info);
		~ProcesserRunGuard();
	};

} //namespace co
