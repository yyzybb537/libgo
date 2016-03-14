#pragma once

#include <memory>

namespace co
{
    /// 读写锁
    class CoRWMutex
    {
        class CoRWMutexImpl;
        std::shared_ptr<CoRWMutexImpl> impl_;

    public:
        class ReadView
        {
            CoRWMutexImpl & ref_;
            ReadView(CoRWMutexImpl&);
            friend class CoRWMutexImpl;

        public:
            void lock();
            bool try_lock();
            bool is_lock();
            void unlock();
        };

        class WriteView
        {
            CoRWMutexImpl & ref_;
            WriteView(CoRWMutexImpl&);
            friend class CoRWMutexImpl;

        public:
            void lock();
            bool try_lock();
            bool is_lock();
            void unlock();
        };

        CoRWMutex();
        ReadView& reader();
        WriteView& writer();
    };

    typedef CoRWMutex co_rwmutex;
    typedef CoRWMutex::ReadView co_rmutex;
    typedef CoRWMutex::WriteView co_wmutex;

} //namespace co
