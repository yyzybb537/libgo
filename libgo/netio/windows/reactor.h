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
    
    struct Entry {
        Processer::SuspendEntry suspendEntry_;
        std::shared_ptr<short int> revents_;
        int idx_;

        Entry() {}
        Entry(Processer::SuspendEntry const& suspendEntry,
            std::shared_ptr<short int> const& revents,
            int idx)
            : suspendEntry_(suspendEntry), revents_(revents), idx_(idx)
        {}

        friend bool operator==(Entry const& lhs, Entry const& rhs) {
            return lhs.idx_ == rhs.idx_ &&
                lhs.revents_ == rhs.revents_ &&
                lhs.suspendEntry_ == rhs.suspendEntry_;
        }
    };

    void Watch(SOCKET sock, short int pollEvent, Entry const& entry);
	
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
		typedef std::unordered_map<SOCKET, std::list<Entry>> Sockets;
		Sockets readers_;
		Sockets writers_;
		Sockets excepters_;
		std::mutex mtx_;
		Interrupter interrupter_;
        std::thread thread_;
        volatile bool exit_;

	public:
        Selector();
        ~Selector();

		void Watch(SOCKET sock, short int pollEvent, Entry const& entry);

        void FdSet(fd_set& set, Sockets & sockets);

		void Perform(fd_set& set, short int pollEvent, Sockets & sockets, std::set<Processer::SuspendEntry> & suspendEntries);

		void ThreadRun();
	};

	typedef std::shared_ptr<Selector> SelectorPtr;
	std::vector<SelectorPtr> selectors_;
	std::mutex mtx_;    
};

} // namespace co