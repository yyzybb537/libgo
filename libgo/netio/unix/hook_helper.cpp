#include "hook_helper.h"
#include "hook.h"

namespace co {

HookHelper& HookHelper::getInstance()
{
    static HookHelper obj;
    return obj;
}

NonBlockingGuard::NonBlockingGuard(FdContextPtr const& fdCtx)
    : fdCtx_(fdCtx)
{
    isNonBlocking_ = fdCtx_->IsNonBlocking();
    if (!isNonBlocking_)
        fdCtx_->SetNonBlocking(true);
}
NonBlockingGuard::~NonBlockingGuard()
{
    if (!isNonBlocking_) {
        ErrnoStore es;
        fdCtx_->SetNonBlocking(false);
    }
}

void HookHelper::OnCreate(int fd, eFdType fdType, bool isNonBlocking,
        SocketAttribute sockAttr)
{
    FdContextPtr ctx(new FdContext(fd, fdType, isNonBlocking, sockAttr));
    Insert(fd, ctx);
}

void HookHelper::OnClose(int fd)
{
    FdSlotPtr slot = GetSlot(fd);
    if (!slot) return ;

    FdContextPtr ctx;
    std::unique_lock<LFLock> lock(slot->lock_);
    slot->ctx_.swap(ctx);
    lock.unlock();

    if (ctx)
        ctx->OnClose();
}

void HookHelper::OnDup(int from, int to)
{
    FdContextPtr ctx = GetFdContext(from);
    if (!ctx) return ;

    Insert(to, ctx->Clone(to));
}

HookHelper::FdSlotPtr HookHelper::GetSlot(int fd)
{
    int bucketIdx = fd & kBucketCount;
    std::unique_lock<std::mutex> lock(bucketMtx_[bucketIdx]);
    auto & bucket = buckets_[bucketIdx];
    auto itr = bucket.find(fd);
    if (itr == bucket.end())
        return FdSlotPtr();
    return itr->second;
}

FdContextPtr HookHelper::GetFdContext(int fd)
{
    FdSlotPtr slot = GetSlot(fd);
    if (!slot) return FdContextPtr();

    std::unique_lock<LFLock> lock(slot->lock_);
    FdContextPtr ctx(slot->ctx_);
    return ctx;
}

void HookHelper::Insert(int fd, FdContextPtr ctx)
{
    int bucketIdx = fd & kBucketCount;
    std::unique_lock<std::mutex> lock(bucketMtx_[bucketIdx]);
    auto & bucket = buckets_[bucketIdx];
    FdSlotPtr & slot = bucket[fd];
    if (!slot) slot.reset(new FdSlot);
    lock.unlock();

    FdContextPtr closedCtx;
    std::unique_lock<LFLock> lock2(slot->lock_);
    closedCtx.swap(slot->ctx_);
    slot->ctx_ = ctx;
    lock2.unlock();

    if (closedCtx)
        closedCtx->OnClose();
}

} // namespace co
