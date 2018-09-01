#include "reactor.h"

namespace co {

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
    (void)CreateIoCompletionPort((HANDLE)sock, iocp_, (ULONG_PTR)this, 0);

    OverlappedEntry* olEntry = new OverlappedEntry;
    olEntry->entry = entry;
    std::unique_ptr<OverlappedEntry> autoDelete(olEntry);

    OVERLAPPED* ol = static_cast<OVERLAPPED*>(olEntry);
    WSABUF dataBuf = {};
    DWORD sent = 0;
    DWORD flags = 0;

    int res = -2;
    if (pollEvent & (POLLIN | POLLERR)) {
        res = WSARecv(sock, &dataBuf, 1, &sent, &flags, ol, nullptr);
    } else if (pollEvent & POLLOUT) {
        res = WSASend(sock, &dataBuf, 1, &sent, 0, ol, nullptr);
    } else {
        autoDelete.release();
        return ePending;
    }

    if (res = 0)
        return eReady;
 
    if (res == -2)
        return eError;

    if (res == -1 && WSAGetLastError() == ERROR_IO_PENDING) {
        autoDelete.release();
        return ePending;
    }

    return eError;
}

void Reactor::ThreadRun()
{
    DWORD  NumberOfBytes = 0;
    ULONG_PTR CompletionKey = 0;
    OVERLAPPED* ol = NULL;
    for (;;)
    {
        GetQueuedCompletionStatus(iocp_, &NumberOfBytes, &CompletionKey, &ol, WSA_INFINITE);
        if (CompletionKey != (ULONG_PTR)this) continue;

        OverlappedEntry* olEntry = (OverlappedEntry*)ol;
        std::unique_ptr<OverlappedEntry> autoDelete(olEntry);
        Processer::Wakeup(olEntry->entry);
        delete olEntry;
    }
}

} //namespace co