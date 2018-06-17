#include <libgo/co_rwmutex.h>
#include <libgo/co_mutex.h>
#include <libgo/scheduler.h>

namespace co
{
    class CoRWMutex::CoRWMutexImpl
    {
        CoMutex mtx_;
        atomic_t<int> readers_c_{0};

    public:
        ReadView r_view;
        WriteView w_view;

        CoRWMutexImpl()
            : r_view(*this), w_view(*this)
        {}

        void r_lock()
        {
            for (;;)
            {
                ++readers_c_;
                if (!mtx_.is_lock()) break ;
                --readers_c_;
                g_Scheduler.SleepSwitch(0);
            }
        }

        bool r_try_lock()
        {
            ++readers_c_;
            if (mtx_.is_lock()) {
                --readers_c_;
                return false;
            }

            return true;
        }

        bool r_is_lock()
        {
            return mtx_.is_lock();
        }

        void r_unlock()
        {
            int rc = --readers_c_;
            (void)rc;
            assert(rc >= 0);
        }

        void w_lock()
        {
            mtx_.lock();
            while (readers_c_)
                g_Scheduler.SleepSwitch(0);
        }

        bool w_try_lock()
        {
            if (!mtx_.try_lock()) return false;
            if (readers_c_) {
                mtx_.unlock();
                return false;
            }

            return true;
        }

        bool w_is_lock()
        {
            return mtx_.is_lock();
        }

        void w_unlock()
        {
            mtx_.unlock();
        }

    };

    CoRWMutex::ReadView::ReadView(CoRWMutexImpl & ref)
        : ref_(ref)
    {}
    void CoRWMutex::ReadView::lock()
    {
        ref_.r_lock();
    }
    bool CoRWMutex::ReadView::try_lock()
    {
        return ref_.r_try_lock();
    }
    bool CoRWMutex::ReadView::is_lock()
    {
        return ref_.r_is_lock();
    }
    void CoRWMutex::ReadView::unlock()
    {
        ref_.r_unlock();
    }

    CoRWMutex::WriteView::WriteView(CoRWMutexImpl & ref)
        : ref_(ref)
    {}
    void CoRWMutex::WriteView::lock()
    {
        ref_.w_lock();
    }
    bool CoRWMutex::WriteView::try_lock()
    {
        return ref_.w_try_lock();
    }
    bool CoRWMutex::WriteView::is_lock()
    {
        return ref_.w_is_lock();
    }
    void CoRWMutex::WriteView::unlock()
    {
        ref_.w_unlock();
    }

    CoRWMutex::CoRWMutex()
        : impl_(new CoRWMutexImpl)
    {}
    CoRWMutex::ReadView& CoRWMutex::reader()
    {
        return impl_->r_view;
    }
    CoRWMutex::WriteView& CoRWMutex::writer()
    {
        return impl_->w_view;
    }

} //namespace co
