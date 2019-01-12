#pragma once
#include <WinSock2.h>
#include <Windows.h>

namespace co {
    int native_select(
        _In_    int                  nfds,
        _Inout_ fd_set               *readfds,
        _Inout_ fd_set               *writefds,
        _Inout_ fd_set               *exceptfds,
        _In_    const struct timeval *timeout
        );

    int native_connect(
        _In_ SOCKET                s,
        _In_ const struct sockaddr *name,
        _In_ int                   namelen
        );

    SOCKET native_accept(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ int             *addrlen
        );

	bool setNonblocking(SOCKET s, bool isNonblocking);
}