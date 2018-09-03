#pragma once
#include "../../common/config.h"
#include "../../scheduler/processer.h"
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>

namespace co {

struct OverlappedEntry : public OVERLAPPED
{
    Processer::SuspendEntry entry;
    std::atomic<long> id;

    static std::atomic<long> s_id;

    OverlappedEntry() : id(++s_id) {}
};


class Reactor
{
public:
	static Reactor& getInstance();

public:
	Reactor();

    enum eWatchResult {
        eError,
        ePending,
        eReady,
    };

    eWatchResult Watch(SOCKET sock, short int pollEvent, Processer::SuspendEntry const& entry);

private:
    void ThreadRun();

private:
    HANDLE iocp_;
};

} // namespace co