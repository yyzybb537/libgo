#include "gtest/gtest.h"
#include <boost/thread.hpp>
#include <boost/timer.hpp>
#include <vector>
#include <list>
#include <atomic>
#include "coroutine.h"
#include "gtest_exit.h"
using namespace std::chrono;
using namespace co;

struct Obj : public co::SharedRefObject, public co::ObjectCounter<Obj>
{
};

TEST(Ptr, Ptr1)
{
    {
        EXPECT_EQ(Obj::getCount(), 0);
        Obj * p = new Obj;
        EXPECT_EQ(Obj::getCount(), 1);

        IncursivePtr<Obj> ptr(p);
        EXPECT_EQ(ptr.use_count(), 2);

        p->DecrementRef();
        EXPECT_EQ(ptr.use_count(), 1);

        IncursivePtr<Obj> ptr2(ptr);
        EXPECT_EQ(ptr.use_count(), 2);

        IncursivePtr<Obj> ptr3 = ptr;
        EXPECT_EQ(ptr.use_count(), 3);

        ptr2.reset();
        EXPECT_EQ(ptr.use_count(), 2);
    }

    EXPECT_EQ(Obj::getCount(), 0);
}

TEST(Ptr, WeakPtr1)
{
    {
        EXPECT_EQ(Obj::getCount(), 0);
        Obj * p = new Obj;
        EXPECT_EQ(Obj::getCount(), 1);

        IncursivePtr<Obj> ptr(p);
        EXPECT_EQ(ptr.use_count(), 2);

        p->DecrementRef();
        EXPECT_EQ(ptr.use_count(), 1);

        WeakPtr<Obj> wp(p);
        EXPECT_EQ(ptr.use_count(), 1);

        auto ptr2 = wp.lock();
        EXPECT_EQ(ptr.use_count(), 2);
    }

    EXPECT_EQ(Obj::getCount(), 0);
}

TEST(Ptr, WeakPtr2)
{
    {
        EXPECT_EQ(Obj::getCount(), 0);
        Obj * p = new Obj;
        EXPECT_EQ(Obj::getCount(), 1);

        IncursivePtr<Obj> ptr(p);
        EXPECT_EQ(ptr.use_count(), 2);

        p->DecrementRef();
        EXPECT_EQ(ptr.use_count(), 1);

        WeakPtr<Obj> wp(p);
        EXPECT_EQ(ptr.use_count(), 1);

        ptr.reset();
        EXPECT_EQ(Obj::getCount(), 0);

        auto ptr2 = wp.lock();
        EXPECT_FALSE(!!ptr2);
    }

    EXPECT_EQ(Obj::getCount(), 0);
}


