#include "win_vc_hook.h"
#include <mswsock.h>
#include <Ws2ipdef.h>
#include "xhook/xhook.h"
#include "../../scheduler/scheduler.h"
#include "reactor.h"

namespace co {

    static bool & native_call() {
        static thread_local bool native = false;
        return native;
    }

    struct NativeGuard {
        NativeGuard() {
            native_call() = true;
        }
        ~NativeGuard() {
            native_call() = false;
        }
    };

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

    int native_select(
        _In_    int                  nfds,
        _Inout_ fd_set               *readfds,
        _Inout_ fd_set               *writefds,
        _Inout_ fd_set               *exceptfds,
        _In_    const struct timeval *timeout
    )
    {
        NativeGuard guard;
        return select_f()(nfds, readfds, writefds, exceptfds, timeout);
    }

    int native_connect(
        _In_ SOCKET                s,
        _In_ const struct sockaddr *name,
        _In_ int                   namelen
    )
    {
        NativeGuard guard;
        return connect_f()(s, name, namelen);
    }

    SOCKET native_accept(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ int             *addrlen
    )
    {
        NativeGuard guard;
        return accept_f()(s, addr, addrlen);
    }

    typedef VOID (WINAPI *Sleep_t)(_In_ DWORD dwMilliseconds);
    static Sleep_t& Sleep_f() {
        static Sleep_t fn = (Sleep_t)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "Sleep");
        if (!fn)
            fn = (Sleep_t)GetProcAddress(GetModuleHandleA("KernelBase.dll"), "Sleep");
        return fn;
    }

    static VOID WINAPI hook_Sleep(_In_ DWORD dwMilliseconds)
    {
        Task *tk = Processer::GetCurrentTask();
        //DebugPrint(dbg_hook, "task(%s) Hook Sleep(dwMilliseconds=%lu).", tk->DebugInfo(), dwMilliseconds);
        if (!tk) {
            Sleep_f()(dwMilliseconds);
            return;
        }

        if (dwMilliseconds > 0)
            Processer::Suspend(std::chrono::milliseconds(dwMilliseconds));

        Processer::StaticCoYield();
    }

    WINSOCK_API_LINKAGE
        int
        WSAAPI
        WSAPoll(
            _Inout_ LPWSAPOLLFD fdArray,
            _In_ ULONG fds,
            _In_ INT timeout
        );

    typedef int (WINAPI *ioctlsocket_t)(
        _In_    SOCKET s,
        _In_    long   cmd,
        _Inout_ u_long *argp
        );
    static ioctlsocket_t& ioctlsocket_f() {
        static ioctlsocket_t fn = (ioctlsocket_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "ioctlsocket");
        return fn;
    }

    static int WINAPI hook_ioctlsocket(
        _In_    SOCKET s,
        _In_    long   cmd,
        _Inout_ u_long *argp
        )
    {
        int res = ioctlsocket_f()(s, cmd, argp);
        if (res == 0) {
            if (cmd == FIONBIO) {
                int val = *(int*)argp;
                int valLen = sizeof(val);
                setsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (const char*)&val, valLen);
            }
        }
        return res;
    }

    typedef int ( WINAPI *WSAIoctl_t)(
        _In_  SOCKET                             s,
        _In_  DWORD                              dwIoControlCode,
        _In_  LPVOID                             lpvInBuffer,
        _In_  DWORD                              cbInBuffer,
        _Out_ LPVOID                             lpvOutBuffer,
        _In_  DWORD                              cbOutBuffer,
        _Out_ LPDWORD                            lpcbBytesReturned,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSAIoctl_t& WSAIoctl_f() {
        static WSAIoctl_t fn = (WSAIoctl_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAIoctl");
        return fn;
    }

    static int WINAPI hook_WSAIoctl(
        _In_  SOCKET                             s,
        _In_  DWORD                              dwIoControlCode,
        _In_  LPVOID                             lpvInBuffer,
        _In_  DWORD                              cbInBuffer,
        _Out_ LPVOID                             lpvOutBuffer,
        _In_  DWORD                              cbOutBuffer,
        _Out_ LPDWORD                            lpcbBytesReturned,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return WSAIoctl_f()(s, dwIoControlCode, lpvInBuffer, cbInBuffer, lpvOutBuffer, cbOutBuffer, 
			lpcbBytesReturned, lpOverlapped, lpCompletionRoutine);
    }

    bool IsNonblocking(SOCKET s)
    {
        int v = 0;
        int vlen = sizeof(v);
        if (0 != getsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (char*)&v, &vlen)) {
            if (WSAENOTSOCK == WSAGetLastError())
                return true;
        }
        return !!v;
    }

	bool setNonblocking(SOCKET s, bool isNonblocking) {
		int v = isNonblocking ? 1 : 0;
        int res = ioctlsocket(s, FIONBIO, (u_long FAR *)&v);
        int err = WSAGetLastError();
        (void)err;
		return (res == NO_ERROR);
	}

    select_t& select_f() {
        static select_t fn = (select_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "select");
        return fn;
    }

    static inline int WINAPI safe_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
    {
        //Task *tk = Processer::GetCurrentTask();
        //DebugPrint(dbg_hook, "task(%s) safe_select(nfds=%d, rfds=%p, wfds=%p, efds=%p).",
        //    tk ? tk->DebugInfo() : "nil", (int)nfds, readfds, writefds, exceptfds);
        static const struct timeval zero_tmv { 0, 0 };
        fd_set *rfds = NULL, *wfds = NULL, *efds = NULL;
        fd_set fds[3];
        if (readfds) {
            fds[0] = *readfds;
            rfds = &fds[0];
        }
        if (writefds) {
            fds[1] = *writefds;
            wfds = &fds[1];
        }
        if (exceptfds) {
            fds[2] = *exceptfds;
            efds = &fds[2];
        }
        int ret = select_f()(nfds, rfds, wfds, efds, &zero_tmv);
        if (ret <= 0) return ret;

        if (readfds) *readfds = fds[0];
        if (writefds) *writefds = fds[1];
        if (exceptfds) *exceptfds = fds[2];
        return ret;
    }

    static int WINAPI hook_select(
        _In_    int                  nfds,
        _Inout_ fd_set               *readfds,
        _Inout_ fd_set               *writefds,
        _Inout_ fd_set               *exceptfds,
        _In_    const struct timeval *timeout
        )
    {
        if (native_call()) {
            return select_f()(nfds, readfds, writefds, exceptfds, timeout);
        }

        static const struct timeval zero_tmv{0, 0};
        long timeout_us = timeout ? (timeout->tv_sec * 1000000 + timeout->tv_usec) : -1;
        int timeout_ms = timeout ? (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;
        Task *tk = Processer::GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) Hook select(nfds=%d, rfds=%p, wfds=%p, efds=%p, timeout=%d).", 
            tk ? tk->DebugInfo() : "nil", (int)nfds, readfds, writefds, exceptfds, timeout_ms);

        if (!tk || timeout_us == 0)
            return select_f()(nfds, readfds, writefds, exceptfds, timeout);

        // async select
        int ret = safe_select(nfds, readfds, writefds, exceptfds);
        if (ret) return ret;
        
        Processer::SuspendEntry entry = (timeout_us == -1) ? 
            Processer::Suspend() : 
            Processer::Suspend(std::chrono::microseconds(timeout_us));

        bool isReady = false;
        fd_set* fd_sets[3] = { readfds, writefds, exceptfds };
        short fd_event[3] = { POLLIN, POLLOUT, POLLERR };

        int n = 0;
        for (int idx = 0; idx < 3; ++idx) {
            fd_set* fds = fd_sets[idx];
            if (!fds) continue;

            n += fds->fd_count;
        }

        short int *arrRevents = new short int[n];
        memset(arrRevents, 0, sizeof(short int) * n);
        std::shared_ptr<short int> revents(arrRevents, [](short int* p) { delete[] p; });

        int i = 0;
        for (int idx = 0; idx < 3; ++idx) {
            fd_set* fds = fd_sets[idx];
            if (!fds) continue;

            for (u_int j = 0; j < fds->fd_count; ++j) {
                Reactor::getInstance().Watch((SOCKET)fds->fd_array[j], fd_event[idx], Reactor::Entry(entry, revents, i++));
            }
        }

        Processer::StaticCoYield();

        fd_set ret_sets[3];

        i = 0;
        ret = 0;
        for (int idx = 0; idx < 3; ++idx) {
            fd_set* fds = fd_sets[idx];
            if (!fds) continue;

            FD_ZERO(&ret_sets[idx]);
            for (int j = 0; j < fds->fd_count; ++j) {
                int k = i + j;
                short int revent = revents.get()[k];
                if (revent & fd_event[idx]) {
                    FD_SET(fds->fd_array[j], &ret_sets[idx]);
                    ++ret;
                }
            }
            
            i += fds->fd_count;
            *fds = ret_sets[idx];
        }

        return ret;
    }

    LPFN_CONNECTEX getConnectExPtr(SOCKET sock)
    {
        DWORD numBytes = 0;
        GUID guid = WSAID_CONNECTEX;
        LPFN_CONNECTEX connectExPtr = NULL;
        int success = ::WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
            (void*)&guid, sizeof(guid), (void*)&connectExPtr, sizeof(connectExPtr),
            &numBytes, NULL, NULL);
        return connectExPtr;
    }

    LPFN_ACCEPTEX getAcceptExPtr(SOCKET sock)
    {
        DWORD numBytes = 0;
        GUID guid = WSAID_ACCEPTEX;
        LPFN_ACCEPTEX acceptExPtr = NULL;
        int success = ::WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
            (void*)&guid, sizeof(guid), (void*)&acceptExPtr, sizeof(acceptExPtr),
            &numBytes, NULL, NULL);
        return acceptExPtr;
    }

    template <typename OriginF, typename ... Args>
    static int connect_mode_hook(OriginF fn, const char* fn_name, 
        SOCKET s, const struct sockaddr *name, int namelen, 
        Args && ... args)
    {
        Task *tk = Processer::GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d).", 
            tk ? tk->DebugInfo() : "nil", fn_name, (int)s, (int)is_nonblocking);
        if (!tk || is_nonblocking)
            return fn(s, name, namelen, std::forward<Args>(args)...);

		setNonblocking(s, true);
		int res = fn(s, name, namelen, std::forward<Args>(args)...);
		if (res == 0) {
			setNonblocking(s, false);
			return 0;
		}

		if (res < 0 && WSAGetLastError() != WSAEINPROGRESS) {
			ErrnoStore es;
			setNonblocking(s, false);
			return res;
		}

        Processer::SuspendEntry entry = Processer::Suspend();
        short int *arrRevents = new short int[1];
        memset(arrRevents, 0, sizeof(short int) * 1);
        std::shared_ptr<short int> revents(arrRevents, [](short int* p) { delete[] p; });

        Reactor::getInstance().Watch(s, POLLOUT, Reactor::Entry(entry, revents, 0));
        
        Processer::StaticCoYield();

        int err = 0;
        int errlen = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
        if (err) {
            WSASetLastError(err);
            return -1;
        }

		WSASetLastError(0);
        return 0;
    }

    template <typename OriginF, typename ... Args>
    static SOCKET accept_mode_hook(OriginF fn, const char* fn_name, 
        SOCKET s, struct sockaddr *addr, int *addrlen,
        Args && ... args)
    {
        Task *tk = Processer::GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d).",
            tk->DebugInfo(), fn_name, (int)s, (int)is_nonblocking);
        if (!tk || is_nonblocking)
            return fn(s, addr, addrlen, std::forward<Args>(args)...);

        Processer::SuspendEntry entry = Processer::Suspend();
        auto result = Reactor::getInstance().Watch(s, POLLIN, entry);
        if (result == Reactor::eError) {
            Processer::Wakeup(entry);
            Processer::StaticCoYield();
            WSASetLastError(WSAEBADF);
            return -1;
        }

		if (result == Reactor::eReady) {
			Processer::Wakeup(entry);
			Processer::StaticCoYield();
			return fn(s, addr, addrlen, std::forward<Args>(args)...);
		}

        // io pending, waiting for
        Processer::StaticCoYield();
		return fn(s, addr, addrlen, std::forward<Args>(args)...);
    }

    enum e_mode_hook_flags
    {
        e_nonblocking_op = 0x1,
        e_no_timeout = 0x1 << 1,
    };

    template <typename R, typename OriginF, typename ... Args>
    static R read_mode_hook(OriginF fn, const char* fn_name, int flags, SOCKET s, Args && ... args)
    {
        Task *tk = Processer::GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d)(flags:%d).",
            tk ? tk->DebugInfo() : "nil", fn_name, (int)s, (int)is_nonblocking, (int)flags);

        if (!tk || is_nonblocking || (flags & e_nonblocking_op))
            return fn(s, std::forward<Args>(args)...);

        int timeout = 0;
        if (!(flags & e_no_timeout)) {
            int timeoutlen = sizeof(timeout);
            getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, &timeoutlen);
        }

        timeval tv = {timeout / 1000, (timeout % 1000) * 1000};
        timeval *ptv = (timeout == 0) ? nullptr : &tv;

        fd_set readfds, exceptfds;
        FD_ZERO(&readfds);
        FD_SET(s, &readfds);
        FD_ZERO(&exceptfds);
        FD_SET(s, &exceptfds);
        int n = select(0, &readfds, nullptr, &exceptfds, ptv);
        if (n <= 0) {
            WSASetLastError(WSAEWOULDBLOCK);
            return -1;
        }

        return fn(s, std::forward<Args>(args)...);
    }

    template <typename R, typename OriginF, typename ... Args>
    static R write_mode_hook(OriginF fn, const char* fn_name, int flags, SOCKET s, Args && ... args)
    {
        Task *tk = Processer::GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d)(flags:%d).",
            tk ? tk->DebugInfo() : "nil", fn_name, (int)s, (int)is_nonblocking, (int)flags);
        if (!tk || is_nonblocking || (flags & e_nonblocking_op))
            return fn(s, std::forward<Args>(args)...);

        int timeout = 0;
        if (!(flags & e_no_timeout)) {
            int timeoutlen = sizeof(timeout);
            getsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, &timeoutlen);
        }

        timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
        timeval *ptv = (timeout == 0) ? nullptr : &tv;

        fd_set writefds, exceptfds;
        FD_ZERO(&writefds);
        FD_SET(s, &writefds);
        FD_ZERO(&exceptfds);
        FD_SET(s, &exceptfds);
        int n = select(0, nullptr, &writefds, &exceptfds, ptv);
        if (n <= 0) {
            WSASetLastError(WSAEWOULDBLOCK);
            return -1;
        }

        return fn(s, std::forward<Args>(args)...);
    }

	connect_t& connect_f() {
        static connect_t fn = (connect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "connect");
        return fn;
    }

    typedef SOCKET (WSAAPI *socket_t)(
            _In_ int af,
            _In_ int type,
            _In_ int protocol
        );
    static socket_t& socket_f() {
        static socket_t fn = (socket_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "socket");
        return fn;
    }

    static SOCKET WSAAPI hook_socket(int af, int type, int protocol)
    {
        return WSASocket(af, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    }

    typedef SOCKET (WSAAPI *WSASocketA_t)(
        _In_ int af,
        _In_ int type,
        _In_ int protocol,
        _In_opt_ LPWSAPROTOCOL_INFOA lpProtocolInfo,
        _In_ GROUP g,
        _In_ DWORD dwFlags
    );
    static WSASocketA_t& WSASocketA_f() {
        static WSASocketA_t fn = (WSASocketA_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASocketA");
        return fn;
    }
    typedef SOCKET(WSAAPI *WSASocketW_t)(
        _In_ int af,
        _In_ int type,
        _In_ int protocol,
        _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
        _In_ GROUP g,
        _In_ DWORD dwFlags
        );
    static WSASocketW_t& WSASocketW_f() {
        static WSASocketW_t fn = (WSASocketW_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASocketW");
        return fn;
    }
    SOCKET WSAAPI hook_WSASocketA(
        _In_ int af,
        _In_ int type,
        _In_ int protocol,
        _In_opt_ LPWSAPROTOCOL_INFOA lpProtocolInfo,
        _In_ GROUP g,
        _In_ DWORD dwFlags
        )
    {
        return WSASocketA_f()(af, type, protocol, lpProtocolInfo, g, dwFlags);
    }
    SOCKET WSAAPI hook_WSASocketW(
        _In_ int af,
        _In_ int type,
        _In_ int protocol,
        _In_opt_ LPWSAPROTOCOL_INFOW lpProtocolInfo,
        _In_ GROUP g,
        _In_ DWORD dwFlags
    )
    {
        return WSASocketW_f()(af, type, protocol, lpProtocolInfo, g, dwFlags);
    }

    static int WINAPI hook_connect(
        _In_ SOCKET                s,
        _In_ const struct sockaddr *name,
        _In_ int                   namelen
        )
    {
        return connect_mode_hook(connect_f(), "connect", s, name, namelen);
    }

    typedef int ( WINAPI *WSAConnect_t)(
        _In_  SOCKET                s,
        _In_  const struct sockaddr *name,
        _In_  int                   namelen,
        _In_  LPWSABUF              lpCallerData,
        _Out_ LPWSABUF              lpCalleeData,
        _In_  LPQOS                 lpSQOS,
        _In_  LPQOS                 lpGQOS
        );
    static WSAConnect_t& WSAConnect_f() {
        static WSAConnect_t fn = (WSAConnect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAConnect");
        return fn;
    }

    static int WINAPI hook_WSAConnect(
        _In_  SOCKET                s,
        _In_  const struct sockaddr *name,
        _In_  int                   namelen,
        _In_  LPWSABUF              lpCallerData,
        _Out_ LPWSABUF              lpCalleeData,
        _In_  LPQOS                 lpSQOS,
        _In_  LPQOS                 lpGQOS
        )
    {
        if (native_call()) {
            return WSAConnect_f()(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
        }

        return connect_mode_hook(WSAConnect_f(), "WSAConnect", s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
    }

    typedef SOCKET (WINAPI *accept_t)(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ int             *addrlen
        );
    accept_t& accept_f() {
        static accept_t fn = (accept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "accept");
        return fn;
    }

    static SOCKET WINAPI hook_accept(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ int             *addrlen
        )
    {
        return read_mode_hook<SOCKET>(accept_f(), "accept", 0, s, addr, addrlen);
    }

    typedef SOCKET (WINAPI *WSAAccept_t)(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ LPINT           addrlen,
        _In_    LPCONDITIONPROC lpfnCondition,
        _In_    DWORD_PTR       dwCallbackData
        );
    static WSAAccept_t& WSAAccept_f() {
        static WSAAccept_t fn = (WSAAccept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAAccept");
        return fn;
    }

    static SOCKET WINAPI hook_WSAAccept(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ LPINT           addrlen,
        _In_    LPCONDITIONPROC lpfnCondition,
        _In_    DWORD_PTR       dwCallbackData
        )
    {
        if (native_call()) {
            return WSAAccept_f()(s, addr, addrlen, lpfnCondition, dwCallbackData);
        }

        return read_mode_hook<SOCKET>(WSAAccept_f(), "WSAAccept", 0, s, addr, addrlen, lpfnCondition, dwCallbackData);
    }

    typedef int ( WINAPI *WSARecv_t)(
        _In_    SOCKET                             s,
        _Inout_ LPWSABUF                           lpBuffers,
        _In_    DWORD                              dwBufferCount,
        _Out_   LPDWORD                            lpNumberOfBytesRecvd,
        _Inout_ LPDWORD                            lpFlags,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSARecv_t& WSARecv_f() {
        static WSARecv_t fn = (WSARecv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecv");
        return fn;
    }

    static int WINAPI hook_WSARecv(
        _In_    SOCKET                             s,
        _Inout_ LPWSABUF                           lpBuffers,
        _In_    DWORD                              dwBufferCount,
        _Out_   LPDWORD                            lpNumberOfBytesRecvd,
        _Inout_ LPDWORD                            lpFlags,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return read_mode_hook<int>(WSARecv_f(), "WSARecv", lpOverlapped ? e_nonblocking_op : 0, s,
            lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine);
    }

    typedef int ( WINAPI *recv_t)(
        _In_  SOCKET s,
        _Out_ char   *buf,
        _In_  int    len,
        _In_  int    flags
        );
    static recv_t& recv_f() {
        static recv_t fn = (recv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recv");
        return fn;
    }

    static int WINAPI hook_recv(
        _In_  SOCKET s,
        _Out_ char   *buf,
        _In_  int    len,
        _In_  int    flags
        )
    {
        return read_mode_hook<int>(recv_f(), "recv", 0, s, buf, len, flags);
    }

    typedef int ( WINAPI *recvfrom_t)(
        _In_        SOCKET          s,
        _Out_       char            *buf,
        _In_        int             len,
        _In_        int             flags,
        _Out_       struct sockaddr *from,
        _Inout_opt_ int             *fromlen
        );
    static recvfrom_t& recvfrom_f() {
        static recvfrom_t fn = (recvfrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recvfrom");
        return fn;
    }

    static int WINAPI hook_recvfrom(
        _In_        SOCKET          s,
        _Out_       char            *buf,
        _In_        int             len,
        _In_        int             flags,
        _Out_       struct sockaddr *from,
        _Inout_opt_ int             *fromlen
        )
    {
        return read_mode_hook<int>(recvfrom_f(), "recvfrom", 0, s, buf, len, flags, from, fromlen);
    }

    typedef int ( WINAPI *WSARecvFrom_t)(
        _In_    SOCKET                             s,
        _Inout_ LPWSABUF                           lpBuffers,
        _In_    DWORD                              dwBufferCount,
        _Out_   LPDWORD                            lpNumberOfBytesRecvd,
        _Inout_ LPDWORD                            lpFlags,
        _Out_   struct sockaddr                    *lpFrom,
        _Inout_ LPINT                              lpFromlen,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSARecvFrom_t& WSARecvFrom_f() {
        static WSARecvFrom_t fn = (WSARecvFrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvFrom");
        return fn;
    }

    static int WINAPI hook_WSARecvFrom(
        _In_    SOCKET                             s,
        _Inout_ LPWSABUF                           lpBuffers,
        _In_    DWORD                              dwBufferCount,
        _Out_   LPDWORD                            lpNumberOfBytesRecvd,
        _Inout_ LPDWORD                            lpFlags,
        _Out_   struct sockaddr                    *lpFrom,
        _Inout_ LPINT                              lpFromlen,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return read_mode_hook<int>(WSARecvFrom_f(), "WSARecvFrom", lpOverlapped ? e_nonblocking_op : 0,
            s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);
    }

    typedef int ( WINAPI *WSARecvMsg_t)(
        _In_    SOCKET                             s,
        _Inout_ LPWSAMSG                           lpMsg,
        _Out_   LPDWORD                            lpdwNumberOfBytesRecvd,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSARecvMsg_t& WSARecvMsg_f() {
        static WSARecvMsg_t fn = (WSARecvMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvMsg");
        return fn;
    }

    static int WINAPI  hook_WSARecvMsg(
        _In_    SOCKET                             s,
        _Inout_ LPWSAMSG                           lpMsg,
        _Out_   LPDWORD                            lpdwNumberOfBytesRecvd,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return read_mode_hook<int>(WSARecvMsg_f(), "WSARecvMsg", lpOverlapped ? e_nonblocking_op : 0,
            s, lpMsg, lpdwNumberOfBytesRecvd, lpOverlapped, lpCompletionRoutine);
    }


    typedef int ( WINAPI *WSASend_t)(
        _In_  SOCKET                             s,
        _In_  LPWSABUF                           lpBuffers,
        _In_  DWORD                              dwBufferCount,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  DWORD                              dwFlags,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSASend_t& WSASend_f() {
        static WSASend_t fn = (WSASend_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASend");
        return fn;
    }

    static int WINAPI hook_WSASend(
        _In_  SOCKET                             s,
        _In_  LPWSABUF                           lpBuffers,
        _In_  DWORD                              dwBufferCount,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  DWORD                              dwFlags,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return write_mode_hook<int>(WSASend_f(), "WSASend", lpOverlapped ? e_nonblocking_op : 0,
            s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine);
    }

    typedef int ( WINAPI *send_t)(
        _In_       SOCKET s,
        _In_ const char   *buf,
        _In_       int    len,
        _In_       int    flags
        );
    static send_t& send_f() {
        static send_t fn = (send_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "send");
        return fn;
    }

    static int WINAPI hook_send(
        _In_       SOCKET s,
        _In_ const char   *buf,
        _In_       int    len,
        _In_       int    flags
        )
    {
        return write_mode_hook<int>(send_f(), "send", 0, s, buf, len, flags);
    }

    typedef int ( WINAPI *sendto_t)(
        _In_       SOCKET                s,
        _In_ const char                  *buf,
        _In_       int                   len,
        _In_       int                   flags,
        _In_       const struct sockaddr *to,
        _In_       int                   tolen
        );
    static sendto_t& sendto_f() {
        static sendto_t fn = (sendto_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "sendto");
        return fn;
    }

    static int WINAPI hook_sendto(
        _In_       SOCKET                s,
        _In_ const char                  *buf,
        _In_       int                   len,
        _In_       int                   flags,
        _In_       const struct sockaddr *to,
        _In_       int                   tolen
        )
    {
        return write_mode_hook<int>(sendto_f(), "sendto", 0, s, buf, len, flags, to, tolen);
    }

    typedef int ( WINAPI *WSASendTo_t)(
        _In_  SOCKET                             s,
        _In_  LPWSABUF                           lpBuffers,
        _In_  DWORD                              dwBufferCount,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  DWORD                              dwFlags,
        _In_  const struct sockaddr              *lpTo,
        _In_  int                                iToLen,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSASendTo_t& WSASendTo_f() {
        static WSASendTo_t fn = (WSASendTo_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendTo");
        return fn;
    }

    static int WINAPI hook_WSASendTo(
        _In_  SOCKET                             s,
        _In_  LPWSABUF                           lpBuffers,
        _In_  DWORD                              dwBufferCount,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  DWORD                              dwFlags,
        _In_  const struct sockaddr              *lpTo,
        _In_  int                                iToLen,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return write_mode_hook<int>(WSASendTo_f(), "WSASendTo", lpOverlapped ? e_nonblocking_op : 0,
            s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iToLen, lpOverlapped, lpCompletionRoutine);
    }

    typedef int ( WINAPI *WSASendMsg_t)(
        _In_  SOCKET                             s,
        _In_  LPWSAMSG                           lpMsg,
        _In_  DWORD                              dwFlags,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSASendMsg_t& WSASendMsg_f() {
        static WSASendMsg_t fn = (WSASendMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendMsg");
        return fn;
    }

    static int WINAPI hook_WSASendMsg(
        _In_  SOCKET                             s,
        _In_  LPWSAMSG                           lpMsg,
        _In_  DWORD                              dwFlags,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return write_mode_hook<int>(WSASendMsg_f(), "WSASendMsg", lpOverlapped ? e_nonblocking_op : 0,
            s, lpMsg, dwFlags, lpNumberOfBytesSent, lpOverlapped, lpCompletionRoutine);
    }

    void initHook()
    {
		XHookRestoreAfterWith();
		XHookTransactionBegin();
		XHookUpdateThread(GetCurrentThread());


        BOOL ok = TRUE;

        ok &= XHookAttach((PVOID*)&Sleep_f(), &hook_Sleep) == NO_ERROR;

        // create socket / winsock
        //ok &= XHookAttach((PVOID*)&socket_f(), &hook_socket) == NO_ERROR;
        //ok &= XHookAttach((PVOID*)&WSASocketA_f(), &hook_WSASocketA) == NO_ERROR;
        //ok &= XHookAttach((PVOID*)&WSASocketW_f(), &hook_WSASocketW) == NO_ERROR;

        // ioctlsocket and select functions.
		ok &= XHookAttach((PVOID*)&ioctlsocket_f(), &hook_ioctlsocket) == NO_ERROR;
        ok &= XHookAttach((PVOID*)&WSAIoctl_f(), &hook_WSAIoctl) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&select_f(), &hook_select) == NO_ERROR;

        // connect-like functions
		ok &= XHookAttach((PVOID*)&connect_f(), &hook_connect) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSAConnect_f(), &hook_WSAConnect) == NO_ERROR;

        // accept-like functions
		ok &= XHookAttach((PVOID*)&accept_f(), &hook_accept) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSAAccept_f(), &hook_WSAAccept) == NO_ERROR;
        
        // recv-like functions
		ok &= XHookAttach((PVOID*)&recv_f(), &hook_recv) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&recvfrom_f(), &hook_recvfrom) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSARecv_f(), &hook_WSARecv) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSARecvFrom_f(), &hook_WSARecvFrom) == NO_ERROR;
        if (WSARecvMsg_f()) // This function minimum support os is Windows 8.
			ok &= XHookAttach((PVOID*)&WSARecvMsg_f(), &hook_WSARecvMsg) == NO_ERROR;

        // send-like functions
		ok &= XHookAttach((PVOID*)&send_f(), &hook_send) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&sendto_f(), &hook_sendto) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSASend_f(), &hook_WSASend) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSASendTo_f(), &hook_WSASendTo) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSASendMsg_f(), &hook_WSASendMsg) == NO_ERROR;
		XHookTransactionCommit();
        
        if (!ok) {
            fprintf(stderr, "Hook failed!");
            exit(1);
        }

    }

} //namespace co
