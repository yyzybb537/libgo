#pragma once
#include "../../common/config.h"
#include <mutex>
#include "fd_context.h"
#include "../../common/spinlock.h"

namespace co {

struct NonBlockingGuard {
    explicit NonBlockingGuard(FdContextPtr const& fdCtx);
    ~NonBlockingGuard();

    FdContextPtr fdCtx_;
    bool isNonBlocking_;
};

class HookHelper
{
public:
    static const int kStaticFdSize = 128;
    static const int kBucketShift = 10;
    static const int kBucketCount = (1 << kBucketShift) - 1;

    struct FdSlot {
        FdContextPtr ctx_;
        LFLock lock_;
    };
    typedef std::shared_ptr<FdSlot> FdSlotPtr;

    static HookHelper& getInstance();

    FdContextPtr GetFdContext(int fd);

public:
    void OnCreate(int fd, eFdType fdType, bool isNonBlocking = false,
            SocketAttribute sockAttr = SocketAttribute());

    // 在syscall之前调用
    void OnClose(int fd);

    // 在syscall之后调用
    void OnDup(int from, int to);

private:
    FdSlotPtr GetSlot(int fd);

    void Insert(int fd, FdContextPtr ctx, bool bNew);

private:
    typedef std::unordered_map<int, FdSlotPtr> Slots;
    Slots buckets_[kBucketCount];
    std::mutex bucketMtx_[kBucketCount];
};

} // namespace co
