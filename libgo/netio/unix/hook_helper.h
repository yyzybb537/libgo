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
    // 有些socket的close行为hook不到, 创建时如果有旧的context直接close掉即可.
    void OnCreate(int fd, eFdType fdType, bool isNonBlocking = false,
            SocketAttribute sockAttr = SocketAttribute());

    // 在syscall之前调用
    void OnClose(int fd);

    // 在syscall之后调用
    void OnDup(int from, int to);

private:
    FdSlotPtr GetSlot(int fd);

    void Insert(int fd, FdContextPtr ctx);

private:
    typedef std::unordered_map<int, FdSlotPtr> Slots;
    Slots buckets_[kBucketCount+1];
    std::mutex bucketMtx_[kBucketCount+1];
};

} // namespace co
