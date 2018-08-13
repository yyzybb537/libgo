#include "fd_context.h"
#include <sys/types.h>
#include <sys/socket.h>
#include "hook.h"
#include <fcntl.h>

namespace co {

FdContext::FdContext(int fd, eFdType fdType, bool isNonBlocking, SocketAttribute sockAttr)
{
    fd_ = fd;
    fdType_ = fdType;
    sockAttr_ = sockAttr;
    isNonBlocking_ = isNonBlocking;
    tcpConnectTimeout_ = 0;
    recvTimeout_ = 0;
    sendTimeout_ = 0;
}
bool FdContext::IsSocket()
{
    return fdType_ == eFdType::eSocket;
}
bool FdContext::IsTcpSocket()
{
    if (!IsSocket()) return false;

    if (!sockAttr_.Initialized()) {
        socklen_t optlen = 4;
        CallWithoutINTR<int>(getsockopt, fd_, SOL_SOCKET, SO_DOMAIN,
                (char*)&sockAttr_.domain_, &optlen);

        optlen = 4;
        CallWithoutINTR<int>(getsockopt, fd_, SOL_SOCKET, SO_TYPE,
                (char*)&sockAttr_.type_, &optlen);

//        optlen = 4;
//        CallWithoutINTR<int>(getsockopt, fd_, SOL_SOCKET, SO_PROTOCOL,
//                (char*)&sockAttr_.protocol_, &optlen);
    }

    return sockAttr_.type_ == SOCK_STREAM &&
        (sockAttr_.domain_ == AF_INET || sockAttr_.domain_ == AF_INET6);
}
bool FdContext::SetNonBlocking(bool isNonBlocking)
{
    int flags = CallWithoutINTR<int>(fcntl_f, fd_, F_GETFL, 0);
    bool old = flags & O_NONBLOCK;
    if (isNonBlocking == old) return old;

    CallWithoutINTR<int>(fcntl_f, fd_, F_SETFL,
            isNonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
    OnSetNonBlocking(isNonBlocking);
    return true;
}
void FdContext::OnSetNonBlocking(bool isNonBlocking)
{
    isNonBlocking_ = isNonBlocking;
}
bool FdContext::IsNonBlocking()
{
    return isNonBlocking_;
}
int FdContext::GetTcpConnectTimeout()
{
    return tcpConnectTimeout_;
}
int FdContext::GetSocketTimeoutMS(int timeoutType)
{
    switch (timeoutType) {
        case SO_RCVTIMEO:
            return recvTimeout_;

        case SO_SNDTIMEO:
            return sendTimeout_;
    }

    return 0;
}
void FdContext::OnSetSocketTimeout(int timeoutType, int milliseconds)
{
    switch (timeoutType) {
        case SO_RCVTIMEO:
            recvTimeout_ = milliseconds;
            break;

        case SO_SNDTIMEO:
            sendTimeout_ = milliseconds;
            break;
    }
}

FdContextPtr FdContext::Clone(int newFd)
{
    FdContextPtr ctx(new FdContext(newFd, fdType_, isNonBlocking_, sockAttr_));
    ctx->tcpConnectTimeout_ = tcpConnectTimeout_;
    ctx->recvTimeout_ = recvTimeout_;
    ctx->sendTimeout_ = sendTimeout_;
    return ctx;
}

void FdContext::OnClose()
{
    ReactorElement::OnClose();
}

} // namespace co
