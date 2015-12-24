#include "platform_adapter.h"
#include "scheduler.h"

namespace co {


	ProcesserRunGuard::ProcesserRunGuard(ThreadLocalInfo &info) : info_(&info)
	{
		info_->scheduler.native = ConvertThreadToFiber(NULL);
	}

	ProcesserRunGuard::~ProcesserRunGuard()
	{
		ConvertFiberToThread();
		info_->scheduler.native = NULL;
	}

} //namespace co
