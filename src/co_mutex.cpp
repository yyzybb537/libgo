#include "co_mutex.h"
#include "scheduler.h"
#include "error.h"
#include <assert.h>

namespace co
{

CoMutex::CoMutex()
    : block_(new BlockObject(1, 1))
{
}

CoMutex::~CoMutex()
{
}

void CoMutex::lock()
{
    block_->CoBlockWait();
}

bool CoMutex::try_lock()
{
    return block_->TryBlockWait();
}

bool CoMutex::is_lock()
{
    return !block_->IsWakeup();
}

void CoMutex::unlock()
{
    if (!block_->Wakeup())
        ThrowError(eCoErrorCode::ec_mutex_double_unlock);
}

} //namespace co
