#include "co_rwmutex.h"
#include "../scheduler/scheduler.h"

namespace co
{

CoRWMutex::CoRWMutex(bool writePriority)
{
    lockState_ = 0;
    writePriority_ = writePriority;
    readView_.self_ = writeView_.self_ = this;
}
CoRWMutex::~CoRWMutex()
{
    assert(lock_.try_lock());
    assert(lockState_ == 0);
    readView_.self_ = writeView_.self_ = nullptr;
}

void CoRWMutex::RLock()
{
    std::unique_lock<LFLock> lock(lock_);

retry:
    if (writePriority_) {
        // 写优先
        if (lockState_ >= 0 && wCv_.empty()) {
            ++lockState_;
            return ;
        }
    } else {
        // 读优先
        if (lockState_ >= 0) {
            ++lockState_;
            return ;
        }
    }

    rCv_.wait(lock);
    goto retry;
}
bool CoRWMutex::RTryLock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (lockState_ >= 0) {
        ++lockState_;
        return true;
    }
    return false;
}
void CoRWMutex::RUnlock()
{
    std::unique_lock<LFLock> lock(lock_);
    assert(lockState_ > 0);
    if (--lockState_ > 0)
        return ;

    TryWakeUp();
}

void CoRWMutex::WLock()
{
    std::unique_lock<LFLock> lock(lock_);

retry:
    if (lockState_ == 0) {
        lockState_ = -1;
        return ;
    }

    wCv_.wait(lock);
    goto retry;
}
bool CoRWMutex::WTryLock()
{
    std::unique_lock<LFLock> lock(lock_);
    if (lockState_ == 0) {
        lockState_ = -1;
        return true;
    }
    return false;
}
void CoRWMutex::WUnlock()
{
    std::unique_lock<LFLock> lock(lock_);
    assert(lockState_ == -1);

    lockState_ = 0;
    TryWakeUp();
}

void CoRWMutex::TryWakeUp()
{
    // 优先唤醒写等待
    if (wCv_.notify_one())
        return ;

    // 唤醒读等待
    rCv_.notify_all();
}

bool CoRWMutex::IsLock()
{
    std::unique_lock<LFLock> lock(lock_);
    return lockState_ == -1;
}

CoRWMutex::ReadView & CoRWMutex::Reader()
{
    return readView_;
}
CoRWMutex::WriteView & CoRWMutex::Writer()
{
    return writeView_;
}
CoRWMutex::ReadView & CoRWMutex::reader()
{
    return readView_;
}
CoRWMutex::WriteView & CoRWMutex::writer()
{
    return writeView_;
}

void CoRWMutex::ReadView::lock()
{
    self_->RLock();
}
bool CoRWMutex::ReadView::try_lock()
{
    return self_->RTryLock();
}
bool CoRWMutex::ReadView::is_lock()
{
    return self_->IsLock();
}
void CoRWMutex::ReadView::unlock()
{
    return self_->RUnlock();
}

void CoRWMutex::WriteView::lock()
{
    self_->WLock();
}
bool CoRWMutex::WriteView::try_lock()
{
    return self_->WTryLock();
}
bool CoRWMutex::WriteView::is_lock()
{
    return self_->IsLock();
}
void CoRWMutex::WriteView::unlock()
{
    self_->WUnlock();
}

} //namespace co
