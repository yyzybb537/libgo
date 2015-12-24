#pragma once

#define co_thread_local thread_local

namespace co {

	struct ThreadLocalInfo;
	struct ProcesserRunGuard
	{
		ThreadLocalInfo *info_;
		ProcesserRunGuard(ThreadLocalInfo &info);
		~ProcesserRunGuard();
	};

} //namespace co
