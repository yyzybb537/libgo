#pragma once
#include <chrono>

// VS2013²»Ö§³Öthread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
#define co_thread_local __declspec(thread)
#else 
#define co_thread_local thread_local
#endif

namespace co
{

#ifdef _WIN32
	typedef std::chrono::microseconds MininumTimeDurationType;
#else
	typedef std::chrono::nanoseconds MininumTimeDurationType;
#endif

} //namespace co