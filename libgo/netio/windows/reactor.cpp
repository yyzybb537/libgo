#include "reactor.h"

namespace co {

std::atomic<long> OverlappedEntry::s_id{ 0 };

Reactor& Reactor::getInstance()
{
    static Reactor obj;
    return obj;
}

Reactor::Reactor()
{
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    DWORD count = si.dwNumberOfProcessors;
    for (DWORD i = 0; i < count; ++i)
        std::thread([=] { this->ThreadRun(); }).detach();
}


Reactor::eWatchResult Reactor::Watch(SOCKET sock, short int pollEvent, Processer::SuspendEntry const& entry)
{
    HANDLE hRet = CreateIoCompletionPort((HANDLE)sock, iocp_, (ULONG_PTR)this, 0);
    int err = WSAGetLastError();

    OverlappedEntry* olEntry = new OverlappedEntry;
    olEntry->entry = entry;
    std::unique_ptr<OverlappedEntry> autoDelete(olEntry);

    OVERLAPPED* ol = static_cast<OVERLAPPED*>(olEntry);
    memset(ol, 0, sizeof(OVERLAPPED));
    WSABUF dataBuf = {};
    DWORD sent = 0;
    DWORD flags = 0;
    long id = olEntry->id;

    int res = -2;
    if (pollEvent & (POLLIN | POLLERR)) {
        DebugPrint(dbg_hook, "reactor -> WSARecv sock=%d id=%ld", (int)sock, id);
        res = WSARecv(sock, &dataBuf, 1, &sent, &flags, ol, nullptr);
    } else if (pollEvent & POLLOUT) {
        DebugPrint(dbg_hook, "reactor -> WSASend sock=%d id=%ld", (int)sock, id);
        res = WSASend(sock, &dataBuf, 1, &sent, 0, ol, nullptr);
    } else {
        autoDelete.release();
        return ePending;
    }

    if (res == 0) {
        DebugPrint(dbg_hook, "reactor -> WSAXXX returns 0, id=%ld", id);
        autoDelete.release();
        return ePending;
    }
 
    err = WSAGetLastError();
    if (res == -1 && err == ERROR_IO_PENDING) {
        DebugPrint(dbg_hook, "reactor -> WSAXXX was pending, id=%ld", id);
        autoDelete.release();
        return ePending;
    }

    DebugPrint(dbg_hook, "reactor -> WSAXXX error %d, id=%ld", err, id);
    return eError;
}

void Reactor::ThreadRun()
{
    DWORD  NumberOfBytes = 0;
    ULONG_PTR CompletionKey = 0;
    OVERLAPPED* ol = NULL;
    for (;;)
    {
        BOOL bRet = GetQueuedCompletionStatus(iocp_, &NumberOfBytes, &CompletionKey, &ol, WSA_INFINITE);
        if (CompletionKey != (ULONG_PTR)this) continue;

        OverlappedEntry* olEntry = (OverlappedEntry*)ol;
        std::unique_ptr<OverlappedEntry> autoDelete(olEntry);
        long id = olEntry->id;
        DebugPrint(dbg_hook, "Complete id=%ld", id);
        Processer::Wakeup(olEntry->entry);
    }
}

} //namespace co