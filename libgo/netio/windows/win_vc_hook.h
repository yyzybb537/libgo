#pragma once
#include <WinSock2.h>
#include <Windows.h>

namespace co {
	typedef int (WINAPI *select_t)(
		_In_    int                  nfds,
		_Inout_ fd_set               *readfds,
		_Inout_ fd_set               *writefds,
		_Inout_ fd_set               *exceptfds,
		_In_    const struct timeval *timeout
		);
	select_t& select_f();

	typedef int (WINAPI *connect_t)(
		_In_ SOCKET                s,
		_In_ const struct sockaddr *name,
		_In_ int                   namelen
		);
	connect_t& connect_f();

	typedef SOCKET(WINAPI *accept_t)(
		_In_    SOCKET          s,
		_Out_   struct sockaddr *addr,
		_Inout_ int             *addrlen
		);
	accept_t& accept_f();

	bool setNonblocking(SOCKET s, bool isNonblocking);
}