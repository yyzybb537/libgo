#include <WinSock2.h>
#include <Windows.h>
#include <xhook.h>
#include "../scheduler.h"

namespace co {

    typedef int (*ioctlsocket_t)(
        _In_    SOCKET s,
        _In_    long   cmd,
        _Inout_ u_long *argp
        );
    static ioctlsocket_t ioctlsocket_f = (ioctlsocket_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "ioctlsocket");

    static int hook_ioctlsocket(
        _In_    SOCKET s,
        _In_    long   cmd,
        _Inout_ u_long *argp
        )
    {
        int ret = ioctlsocket_f(s, cmd, argp);
        int err = WSAGetLastError();
        if (ret == 0 && cmd == FIONBIO) {
            int v = *argp;
            setsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (const char*)&v, sizeof(v));
        }
        WSASetLastError(err);
        return ret;
    }

    typedef int (*WSAIoctl_t)(
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
    static WSAIoctl_t WSAIoctl_f = (WSAIoctl_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAIoctl");

    static int hook_WSAIoctl(
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
        int ret = WSAIoctl_f(s, dwIoControlCode, lpvInBuffer, cbInBuffer, lpvOutBuffer, cbOutBuffer, lpcbBytesReturned, lpOverlapped, lpCompletionRoutine);
        //int err = WSAGetLastError();
        //if (ret == 0 && cmd == FIONBIO) {
        //    int v = *argp;
        //    setsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (const char*)&v, sizeof(v));
        //}
        //WSASetLastError(err);
        return ret;
    }

    bool SetNonblocking(SOCKET s, bool is_nonblocking)
    {
        u_long v = is_nonblocking ? 1 : 0;
        return ioctlsocket(s, FIONBIO, &v) == 0;
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

    typedef int (*select_t)(
        _In_    int                  nfds,
        _Inout_ fd_set               *readfds,
        _Inout_ fd_set               *writefds,
        _Inout_ fd_set               *exceptfds,
        _In_    const struct timeval *timeout
        );
    static select_t select_f = (select_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "select");

    static inline int safe_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
    {
        //Task *tk = g_Scheduler.GetCurrentTask();
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
        int ret = select_f(nfds, rfds, wfds, efds, &zero_tmv);
        if (ret <= 0) return ret;

        if (readfds) *readfds = fds[0];
        if (writefds) *writefds = fds[1];
        if (exceptfds) *exceptfds = fds[2];
        return ret;
    }

    static int hook_select(
        _In_    int                  nfds,
        _Inout_ fd_set               *readfds,
        _Inout_ fd_set               *writefds,
        _Inout_ fd_set               *exceptfds,
        _In_    const struct timeval *timeout
        )
    {
        static const struct timeval zero_tmv{0, 0};
        int timeout_ms = timeout ? (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;
        Task *tk = g_Scheduler.GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) Hook select(nfds=%d, rfds=%p, wfds=%p, efds=%p, timeout=%d).", 
            tk ? tk->DebugInfo() : "nil", (int)nfds, readfds, writefds, exceptfds, timeout_ms);

        if (!tk || !timeout_ms)
            return select_f(nfds, readfds, writefds, exceptfds, timeout);

        // async select
        int ret = safe_select(nfds, readfds, writefds, exceptfds);
        if (ret) return ret;
        
        ULONGLONG start_time = GetTickCount64();
        int delta_time = 1;
        while (-1 == timeout_ms || GetTickCount64() - start_time < timeout_ms)
        {
            ret = safe_select(nfds, readfds, writefds, exceptfds);
            if (ret > 0) return ret;

            if (exceptfds) {
                // 因为windows的select, 仅在事先监听时才能捕获到error, 因此此处需要手动check
                fd_set e_result;
                FD_ZERO(&e_result);
                for (u_int i = 0; i < exceptfds->fd_count; ++i)
                {
                    SOCKET fd = exceptfds->fd_array[i];
                    int err = 0;
                    int errlen = sizeof(err);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
                    if (err) {
                        FD_SET(fd, &e_result);
                    }
                }

                if (e_result.fd_count > 0) {
                    // Some errors were happened.
                    if (readfds) FD_ZERO(readfds);
                    if (writefds) FD_ZERO(writefds);
                    *exceptfds = e_result;
                    return e_result.fd_count;
                }
            }

            g_Scheduler.SleepSwitch(delta_time);
            if (delta_time < 16)
                delta_time <<= 2;
        }

        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
        return 0;
    }

    template <typename OriginF, typename ... Args>
    static int connect_mode_hook(OriginF fn, const char* fn_name, SOCKET s, Args && ... args)
    {
        Task *tk = g_Scheduler.GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d).", 
            tk ? tk->DebugInfo() : "nil", fn_name, (int)s, (int)is_nonblocking);
        if (!tk || is_nonblocking)
            return fn(s, std::forward<Args>(args)...);

        // async connect
        if (!SetNonblocking(s, true))
            return fn(s, std::forward<Args>(args)...);

        int ret = fn(s, std::forward<Args>(args)...);
        if (ret == 0) return 0;

        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err && WSAEINPROGRESS != err)
            return ret;

        fd_set wfds = {}, efds = {};
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(s, &wfds);
        FD_SET(s, &efds);
        select(1, NULL, &wfds, &efds, NULL);
        if (!FD_ISSET(s, &efds) && FD_ISSET(s, &wfds)) {
            SetNonblocking(s, false);
            return 0;
        }

        err = 0;
        int errlen = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
        if (err) {
            SetNonblocking(s, false);
            WSASetLastError(err);
            return SOCKET_ERROR;
        }

        SetNonblocking(s, false);
        return 0;
    }

    typedef int (*connect_t)(
        _In_ SOCKET                s,
        _In_ const struct sockaddr *name,
        _In_ int                   namelen
        );
    static connect_t connect_f = (connect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "connect");

    static int hook_connect(
        _In_ SOCKET                s,
        _In_ const struct sockaddr *name,
        _In_ int                   namelen
        )
    {
        return connect_mode_hook(connect_f, "connect", s, name, namelen);
    }

    typedef int (*WSAConnect_t)(
        _In_  SOCKET                s,
        _In_  const struct sockaddr *name,
        _In_  int                   namelen,
        _In_  LPWSABUF              lpCallerData,
        _Out_ LPWSABUF              lpCalleeData,
        _In_  LPQOS                 lpSQOS,
        _In_  LPQOS                 lpGQOS
        );
    static WSAConnect_t WSAConnect_f = (WSAConnect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAConnect");

    static int hook_WSAConnect(
        _In_  SOCKET                s,
        _In_  const struct sockaddr *name,
        _In_  int                   namelen,
        _In_  LPWSABUF              lpCallerData,
        _Out_ LPWSABUF              lpCalleeData,
        _In_  LPQOS                 lpSQOS,
        _In_  LPQOS                 lpGQOS
        )
    {
        return connect_mode_hook(WSAConnect_f, "WSAConnect", s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
    }

    enum e_mode_hook_flags
    {
        e_nonblocking_op    = 0x1,
        e_no_timeout        = 0x1 << 1,
    };

    template <typename R, typename OriginF, typename ... Args>
    static R read_mode_hook(OriginF fn, const char* fn_name, int flags, SOCKET s, Args && ... args)
    {
        Task *tk = g_Scheduler.GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d)(flags:%d).",
            tk ? tk->DebugInfo() : "nil", fn_name, (int)s, (int)is_nonblocking, (int)flags);
        if (!tk || is_nonblocking || (flags & e_nonblocking_op))
            return fn(s, std::forward<Args>(args)...);

        // async WSARecv
        if (!SetNonblocking(s, true))
            return fn(s, std::forward<Args>(args)...);

        R ret = fn(s, std::forward<Args>(args)...);
        if (ret != -1) {    // accept返回SOCKET类型，是无符号整数，所以此处不可判断小于0.
            SetNonblocking(s, false);
            return ret;
        }

        // If connection is closed, the Bytes will setted 0, and ret is 0, and WSAGetLastError() returns 0.
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err && WSAEINPROGRESS != err) {
            SetNonblocking(s, false);
            WSASetLastError(err);
            return ret;
        }

        // wait data arrives.
        int timeout = 0;
        if (!(flags & e_no_timeout)) {
            int timeoutlen = sizeof(timeout);
            getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, &timeoutlen);
        }

        timeval tm{ timeout / 1000, timeout % 1000 * 1000 };
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        select(1, &rfds, NULL, NULL, timeout ? &tm : NULL);

        ret = fn(s, std::forward<Args>(args)...);
        err = WSAGetLastError();
        SetNonblocking(s, false);
        WSASetLastError(err);
        return ret;
    }

    typedef SOCKET (*accept_t)(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ int             *addrlen
        );
    static accept_t accept_f = (accept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "accept");

    static SOCKET hook_accept(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ int             *addrlen
        )
    {
        return read_mode_hook<SOCKET>(accept_f, "accept", e_no_timeout, s, addr, addrlen);
    }

    typedef SOCKET (*WSAAccept_t)(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ LPINT           addrlen,
        _In_    LPCONDITIONPROC lpfnCondition,
        _In_    DWORD_PTR       dwCallbackData
        );
    static WSAAccept_t WSAAccept_f = (WSAAccept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAAccept");

    static SOCKET hook_WSAAccept(
        _In_    SOCKET          s,
        _Out_   struct sockaddr *addr,
        _Inout_ LPINT           addrlen,
        _In_    LPCONDITIONPROC lpfnCondition,
        _In_    DWORD_PTR       dwCallbackData
        )
    {
        return read_mode_hook<SOCKET>(WSAAccept_f, "WSAAccept", e_no_timeout, s, addr, addrlen, lpfnCondition, dwCallbackData);
    }

    typedef int (*WSARecv_t)(
        _In_    SOCKET                             s,
        _Inout_ LPWSABUF                           lpBuffers,
        _In_    DWORD                              dwBufferCount,
        _Out_   LPDWORD                            lpNumberOfBytesRecvd,
        _Inout_ LPDWORD                            lpFlags,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSARecv_t WSARecv_f = (WSARecv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecv");

    static int hook_WSARecv(
        _In_    SOCKET                             s,
        _Inout_ LPWSABUF                           lpBuffers,
        _In_    DWORD                              dwBufferCount,
        _Out_   LPDWORD                            lpNumberOfBytesRecvd,
        _Inout_ LPDWORD                            lpFlags,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return read_mode_hook<int>(WSARecv_f, "WSARecv", lpOverlapped ? e_nonblocking_op : 0, s,
            lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine);
    }

    typedef int (*recv_t)(
        _In_  SOCKET s,
        _Out_ char   *buf,
        _In_  int    len,
        _In_  int    flags
        );
    static recv_t recv_f = (recv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recv");

    static int hook_recv(
        _In_  SOCKET s,
        _Out_ char   *buf,
        _In_  int    len,
        _In_  int    flags
        )
    {
        return read_mode_hook<int>(recv_f, "recv", 0, s, buf, len, flags);
    }

    typedef int (*recvfrom_t)(
        _In_        SOCKET          s,
        _Out_       char            *buf,
        _In_        int             len,
        _In_        int             flags,
        _Out_       struct sockaddr *from,
        _Inout_opt_ int             *fromlen
        );
    static recvfrom_t recvfrom_f = (recvfrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recvfrom");

    static int hook_recvfrom(
        _In_        SOCKET          s,
        _Out_       char            *buf,
        _In_        int             len,
        _In_        int             flags,
        _Out_       struct sockaddr *from,
        _Inout_opt_ int             *fromlen
        )
    {
        return read_mode_hook<int>(recvfrom_f, "recvfrom", 0, s, buf, len, flags, from, fromlen);
    }

    typedef int (*WSARecvFrom_t)(
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
    static WSARecvFrom_t WSARecvFrom_f = (WSARecvFrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvFrom");

    static int hook_WSARecvFrom(
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
        return read_mode_hook<int>(WSARecvFrom_f, "WSARecvFrom", lpOverlapped ? e_nonblocking_op : 0,
            s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);
    }

    typedef int (*WSARecvMsg_t)(
        _In_    SOCKET                             s,
        _Inout_ LPWSAMSG                           lpMsg,
        _Out_   LPDWORD                            lpdwNumberOfBytesRecvd,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSARecvMsg_t WSARecvMsg_f = (WSARecvMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvMsg");

    static int hook_WSARecvMsg(
        _In_    SOCKET                             s,
        _Inout_ LPWSAMSG                           lpMsg,
        _Out_   LPDWORD                            lpdwNumberOfBytesRecvd,
        _In_    LPWSAOVERLAPPED                    lpOverlapped,
        _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return read_mode_hook<int>(WSARecvMsg_f, "WSARecvMsg", lpOverlapped ? e_nonblocking_op : 0,
            s, lpMsg, lpdwNumberOfBytesRecvd, lpOverlapped, lpCompletionRoutine);
    }

    template <typename R, typename OriginF, typename ... Args>
    static R write_mode_hook(OriginF fn, const char* fn_name, int flags, SOCKET s, Args && ... args)
    {
        Task *tk = g_Scheduler.GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d)(flags:%d).",
            tk ? tk->DebugInfo() : "nil", fn_name, (int)s, (int)is_nonblocking, (int)flags);
        if (!tk || is_nonblocking || (flags & e_nonblocking_op))
            return fn(s, std::forward<Args>(args)...);

        // async WSARecv
        if (!SetNonblocking(s, true))
            return fn(s, std::forward<Args>(args)...);

        R ret = fn(s, std::forward<Args>(args)...);
        if (ret != -1) {
            SetNonblocking(s, false);
            return ret;
        }

        // If connection is closed, the Bytes will setted 0, and ret is 0, and WSAGetLastError() returns 0.
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err && WSAEINPROGRESS != err) {
            SetNonblocking(s, false);
            WSASetLastError(err);
            return ret;
        }

        // wait data arrives.
        int timeout = 0;
        if (!(flags & e_no_timeout)) {
            int timeoutlen = sizeof(timeout);
            getsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, &timeoutlen);
        }

        timeval tm{ timeout / 1000, timeout % 1000 * 1000 };
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(s, &wfds);
        select(1, NULL, &wfds, NULL, timeout ? &tm : NULL);

        ret = fn(s, std::forward<Args>(args)...);
        err = WSAGetLastError();
        SetNonblocking(s, false);
        WSASetLastError(err);
        return ret;
    }

    typedef int (*WSASend_t)(
        _In_  SOCKET                             s,
        _In_  LPWSABUF                           lpBuffers,
        _In_  DWORD                              dwBufferCount,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  DWORD                              dwFlags,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSASend_t WSASend_f = (WSASend_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASend");

    static int hook_WSASend(
        _In_  SOCKET                             s,
        _In_  LPWSABUF                           lpBuffers,
        _In_  DWORD                              dwBufferCount,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  DWORD                              dwFlags,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return write_mode_hook<int>(WSASend_f, "WSASend", lpOverlapped ? e_nonblocking_op : 0,
            s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine);
    }

    typedef int (*send_t)(
        _In_       SOCKET s,
        _In_ const char   *buf,
        _In_       int    len,
        _In_       int    flags
        );
    static send_t send_f = (send_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "send");

    static int hook_send(
        _In_       SOCKET s,
        _In_ const char   *buf,
        _In_       int    len,
        _In_       int    flags
        )
    {
        return write_mode_hook<int>(send_f, "send", 0, s, buf, len, flags);
    }

    typedef int (*sendto_t)(
        _In_       SOCKET                s,
        _In_ const char                  *buf,
        _In_       int                   len,
        _In_       int                   flags,
        _In_       const struct sockaddr *to,
        _In_       int                   tolen
        );
    static sendto_t sendto_f = (sendto_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "sendto");

    static int hook_sendto(
        _In_       SOCKET                s,
        _In_ const char                  *buf,
        _In_       int                   len,
        _In_       int                   flags,
        _In_       const struct sockaddr *to,
        _In_       int                   tolen
        )
    {
        return write_mode_hook<int>(sendto_f, "sendto", 0, s, buf, len, flags, to, tolen);
    }

    typedef int (*WSASendTo_t)(
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
    static WSASendTo_t WSASendTo_f = (WSASendTo_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendTo");

    static int hook_WSASendTo(
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
        return write_mode_hook<int>(WSASendTo_f, "WSASendTo", lpOverlapped ? e_nonblocking_op : 0,
            s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iToLen, lpOverlapped, lpCompletionRoutine);
    }

    typedef int (*WSASendMsg_t)(
        _In_  SOCKET                             s,
        _In_  LPWSAMSG                           lpMsg,
        _In_  DWORD                              dwFlags,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        );
    static WSASendMsg_t WSASendMsg_f = (WSASendMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendMsg");

    static int hook_WSASendMsg(
        _In_  SOCKET                             s,
        _In_  LPWSAMSG                           lpMsg,
        _In_  DWORD                              dwFlags,
        _Out_ LPDWORD                            lpNumberOfBytesSent,
        _In_  LPWSAOVERLAPPED                    lpOverlapped,
        _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
        )
    {
        return write_mode_hook<int>(WSASendMsg_f, "WSASendMsg", lpOverlapped ? e_nonblocking_op : 0,
            s, lpMsg, dwFlags, lpNumberOfBytesSent, lpOverlapped, lpCompletionRoutine);
    }

    void coroutine_hook_init()
    {
		XHookRestoreAfterWith();
		XHookTransactionBegin();
		XHookUpdateThread(GetCurrentThread());

        BOOL ok = true;
        // ioctlsocket and select functions.
		ok &= XHookAttach((PVOID*)&ioctlsocket_f, &hook_ioctlsocket) == NO_ERROR;
        //ok &= Mhook_SetHook((PVOID*)&WSAIoctl_f, &hook_WSAIoctl);
		ok &= XHookAttach((PVOID*)&select_f, &hook_select) == NO_ERROR;

        // connect-like functions
		ok &= XHookAttach((PVOID*)&connect_f, &hook_connect) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSAConnect_f, &hook_WSAConnect) == NO_ERROR;

        // accept-like functions
		ok &= XHookAttach((PVOID*)&accept_f, &hook_accept) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSAAccept_f, &hook_WSAAccept) == NO_ERROR;
        
        // recv-like functions
		ok &= XHookAttach((PVOID*)&recv_f, &hook_recv) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&recvfrom_f, &hook_recvfrom) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSARecv_f, &hook_WSARecv) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSARecvFrom_f, &hook_WSARecvFrom) == NO_ERROR;
        if (WSARecvMsg_f) // This function minimum support os is Windows 8.
			ok &= XHookAttach((PVOID*)&WSARecvMsg_f, &hook_WSARecvMsg) == NO_ERROR;

        // send-like functions
		ok &= XHookAttach((PVOID*)&send_f, &hook_send) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&sendto_f, &hook_sendto) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSASend_f, &hook_WSASend) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSASendTo_f, &hook_WSASendTo) == NO_ERROR;
		ok &= XHookAttach((PVOID*)&WSASendMsg_f, &hook_WSASendMsg) == NO_ERROR;
		XHookTransactionCommit();
        
        if (!ok) {
            fprintf(stderr, "Hook failed!");
            exit(1);
        }
    }

} //namespace co