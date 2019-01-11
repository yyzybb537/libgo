#pragma once
#include "../../common/config.h"
#include "../../scheduler/processer.h"
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <unordered_map>
#include <mutex>

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
	class Interrupter
	{
		SOCKET writer_;
		SOCKET reader_;
		SOCKET accepter_;

	public:
		Interrupter();
		~Interrupter();

		void interrupter();

		void reset();

		SOCKET socket();
	};

	class Selector
	{
		typedef std::unordered_map<SOCKET, std::vector<Processer::SuspendEntry>> Sockets;
		Sockets readers_;
		Sockets writers_;
		Sockets excepters_;
		std::mutex mtx_;
		Interrupter interrupter_;

	public:
		Selector();

		eWatchResult Watch(SOCKET sock, short int pollEvent, Processer::SuspendEntry const& entry);

		void Perform(fd_set& set, Sockets & sockets);

		void ThreadRun();
	};

	typedef std::shared_ptr<Selector> SelectorPtr;
	std::vector<SelectorPtr> selectors_;
	std::mutex mtx_;
};

} // namespace co