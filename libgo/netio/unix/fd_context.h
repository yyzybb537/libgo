#pragma once
#include "../../common/config.h"
#include "reactor_element.h"

namespace co {

class FdContext;
typedef std::shared_ptr<FdContext> FdContextPtr;

enum class eFdType : uint8_t {
    eSocket,
    ePipe,
};
const char* FdType2Str(eFdType fdType);

struct SocketAttribute {
    int domain_ = -1;
    int type_ = -1;
    int protocol_ = -1;

    SocketAttribute() {}
    SocketAttribute(int domain, int type, int protocol) 
        : domain_(domain), type_(type), protocol_(protocol)
    {}
    bool Initialized() const { return domain_ != -1; }
};

uint32_t PollEvent2EpollEvent(short int pollEvent);

short int EpollEvent2PollEvent(uint32_t epollEvent);

template <typename R, typename F, typename ... Args>
static R CallWithoutINTR(F f, Args && ... args)
{
retry:
    R res = f(std::forward<Args>(args)...);
    if (res == -1 && errno == EINTR)
        goto retry;
    return res;
}

class FdContext : public ReactorElement
{
public:
    explicit FdContext(int fd, eFdType fdType, bool isNonBlocking, SocketAttribute sockAttr);

    bool IsTcpSocket();

    bool IsSocket();

    bool IsNonBlocking();

    int GetTcpConnectTimeout();

    int GetSocketTimeoutMS(int timeoutType);

public:
    void OnSetNonBlocking(bool isNonBlocking);

    void OnSetSocketTimeout(int timeoutType, int milliseconds);

    FdContextPtr Clone(int newFd);

    void OnClose();

public:
    bool SetNonBlocking(bool isNonBlocking);

private:
    int fd_;
    eFdType fdType_;
    SocketAttribute sockAttr_;
    bool isNonBlocking_;
    int tcpConnectTimeout_;
    int recvTimeout_;
    int sendTimeout_;
};

} // namespace co
