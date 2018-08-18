#include "fd_context.h"
#include <sys/types.h>
#include <sys/socket.h>
#include "hook.h"
#include <fcntl.h>
#include <poll.h>
#if defined(LIBGO_SYS_Linux)
#include <sys/epoll.h>
#elif defined(LIBGO_SYS_FreeBSD)
#include <sys/event.h>
#include <sys/time.h>
#endif

namespace co {

const char* FdType2Str(eFdType fdType)
{
    switch (fdType) {
        LIBGO_E2S_DEFINE(eFdType::eSocket);
        LIBGO_E2S_DEFINE(eFdType::ePipe);
        default:
            return "Unkown FdType";
    }
}

FdContext::FdContext(int fd, eFdType fdType, bool isNonBlocking, SocketAttribute sockAttr)
    : ReactorElement(fd)
{
    fd_ = fd;
    fdType_ = fdType;
    sockAttr_ = sockAttr;
    isNonBlocking_ = isNonBlocking;
    tcpConnectTimeout_ = 0;
    recvTimeout_ = 0;
    sendTimeout_ = 0;
    DebugPrint(dbg_fd_ctx, "create FdContext(fd = %d, type = %s, isNonBlocking = %d, attr(%d,%d,%d)",
            fd, FdType2Str(fdType), (int)isNonBlocking, sockAttr_.domain_, sockAttr.type_, sockAttr_.protocol_);
}
bool FdContext::IsSocket()
{
    return fdType_ == eFdType::eSocket;
}
bool FdContext::IsTcpSocket()
{
    if (!IsSocket()) return false;

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
void FdContext::SetTcpConnectTimeout(int milliseconds)
{
    tcpConnectTimeout_ = milliseconds;
}
int FdContext::GetTcpConnectTimeout()
{
    return tcpConnectTimeout_;
}
long FdContext::GetSocketTimeoutMicroSeconds(int timeoutType)
{
    switch (timeoutType) {
        case SO_RCVTIMEO:
            return recvTimeout_;

        case SO_SNDTIMEO:
            return sendTimeout_;
    }

    return 0;
}
void FdContext::OnSetSocketTimeout(int timeoutType, int microseconds)
{
    switch (timeoutType) {
        case SO_RCVTIMEO:
            recvTimeout_ = microseconds;
            break;

        case SO_SNDTIMEO:
            sendTimeout_ = microseconds;
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
    DebugPrint(dbg_fd_ctx, "close FdContext(fd = %d)", fd_);
}

} // namespace co
