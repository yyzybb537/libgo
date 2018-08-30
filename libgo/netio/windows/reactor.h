#pragma once
#include "../../common/config.h"
#include "../../scheduler/processer.h"
#include <WinSock2.h>
#include <Windows.h>

namespace co {

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