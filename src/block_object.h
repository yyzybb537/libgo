#pragma once
#include "ts_queue.h"
#include "task.h"

namespace co
{

class BlockObject
{
protected:
    friend class Processer;
    std::size_t wakeup_;
    std::size_t max_wakeup_;
    TSQueue<Task, false> wait_queue_;
    LFLock lock_;

public:
    explicit BlockObject(std::size_t init_wakeup = 0, std::size_t max_wakeup = -1);
    ~BlockObject();

    void CoBlockWait();

    bool TryBlockWait();

    bool Wakeup();

    bool IsWakeup();

private:
    bool AddWaitTask(Task* tk);
};

} //namespace co
