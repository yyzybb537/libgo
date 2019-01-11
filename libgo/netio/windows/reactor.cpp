#define FD_SETSIZE 1024
#include "reactor.h"
#include "win_vc_hook.h"

namespace co {

Reactor& Reactor::getInstance()
{
    static Reactor obj;
    return obj;
}

Reactor::Reactor()
{
}


Reactor::eWatchResult Reactor::Watch(SOCKET sock, short int pollEvent, Processer::SuspendEntry const& entry)
{
	SelectorPtr selector;

	{
		std::unique_lock<std::mutex> lock(mtx_);
		int idx = sock / (FD_SETSIZE - 1);
		assert(idx < 2048);  // protect
		for (int i = selectors_.size(); i <= idx; ++i) {
			selectors_.push_back(std::make_shared<Selector>());
		}
		selector = selectors_[idx];
	}
	
	return selector->Watch(sock, pollEvent, entry);
}

Reactor::Selector::Selector() 
{
	std::thread([this] { this->ThreadRun(); }).detach();
}


void Reactor::Selector::ThreadRun()
{
	for (;;) {
		fd_set readfds, writefds, exceptfds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);
		FD_SET(interrupter_.socket(), &readfds);

		{
			std::unique_lock<std::mutex> lock(mtx_);
			for (auto & kv : readers_) {
				FD_SET(kv.first, &readfds);
			}
			for (auto & kv : writers_) {
				FD_SET(kv.first, &writefds);
			}
			for (auto & kv : excepters_) {
				FD_SET(kv.first, &exceptfds);
			}
		}
		
		int ignored = 0;
		timeval* blocking = nullptr;
		int n = select_f()(ignored, &readfds, &writefds, &exceptfds, blocking);
		if (n > 0 && FD_ISSET(interrupter_.socket(), &readfds)) {
			interrupter_.reset();
			FD_CLR(interrupter_.socket(), &readfds);
			--n;
		}

		if (!n)
			continue;

		std::unique_lock<std::mutex> lock(mtx_);
		Perform(readfds, readers_);
		Perform(writefds, writers_);
		Perform(exceptfds, excepters_);
	}
}

void Reactor::Selector::Perform(fd_set& set, Sockets & sockets)
{
	for (u_int i = 0; i < set.fd_count; ++i) {
		SOCKET sock = set.fd_array[i];
		auto it = sockets.find(sock);
		if (sockets.end() == it)
			continue;

		for (auto & entry : it->second) {
			Processer::Wakeup(entry);
		}
		sockets.erase(it);
	}
}

Reactor::eWatchResult Reactor::Selector::Watch(SOCKET sock, short int pollEvent, Processer::SuspendEntry const& entry)
{
	fd_set readfds, writefds, exceptfds;
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	if (pollEvent & POLLIN) {
		FD_SET(sock, &readfds);
	}
	if (pollEvent & POLLOUT) {
		FD_SET(sock, &writefds);
	}
	FD_SET(sock, &exceptfds);
	timeval nonblocking = { 0, 0 };
	int n = select_f()(0, &readfds, &writefds, &exceptfds, &nonblocking);
	if (n < 0) {
		return Reactor::eError;
	}
	if (n > 0) {
		return Reactor::eReady;
	}

	std::unique_lock<std::mutex> lock(mtx_);
	if (pollEvent & POLLIN) {
		readers_[sock].push_back(entry);
	}
	if (pollEvent & POLLOUT) {
		writers_[sock].push_back(entry);
	}
	excepters_[sock].push_back(entry);
	lock.unlock();

	interrupter_.interrupter();
}

Reactor::Interrupter::Interrupter()
{
	SOCKET accepter_ = ::socket(AF_INET, SOCK_STREAM, 0);
	assert(accepter_ != INVALID_SOCKET);
	
	sockaddr_in addr;
	int addrLen = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int res = ::bind(accepter_, (const sockaddr*)&addr, sizeof(addr));
	(void)res;
	assert(res == 0);

	res = getsockname(accepter_, (sockaddr*)&addr, &addrLen);
	assert(res == 0);

	if (addr.sin_addr.s_addr == htonl(INADDR_ANY))
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	::listen(accepter_, 1);

	reader_ = ::socket(AF_INET, SOCK_STREAM, 0);
	assert(reader_ != INVALID_SOCKET);

	res = connect_f()(reader_, (const sockaddr*)&addr, addrLen);
	assert(res == 0);

	writer_ = accept_f()(accepter_, (sockaddr*)&addr, &addrLen);
	assert(writer_ != INVALID_SOCKET);

	bool bRet = setNonblocking(writer_, true);
	assert(bRet);
	bool bRet = setNonblocking(reader_, true);
	assert(bRet);

	::closesocket(accepter_);
	accepter_ = INVALID_SOCKET;
}
Reactor::Interrupter::~Interrupter()
{
	::closesocket(accepter_);
	::closesocket(writer_);
	::closesocket(reader_);
}

void Reactor::Interrupter::interrupter()
{
	::send(writer_, "A", 1, 0);
}

void Reactor::Interrupter::reset()
{
	char buf[4096];
	while (::recv(reader_, buf, sizeof(buf), 0) < sizeof(buf));
}

SOCKET Reactor::Interrupter::socket()
{
	return writer_;
}

} //namespace co