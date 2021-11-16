#include <iostream>
#include <unistd.h>
#include <gtest/gtest.h>
// #define OPEN_ROUTINE_SYNC_DEBUG 1
#include "coroutine.h"
#include <boost/thread.hpp>
#include "gtest_exit.h"
using namespace std;
using namespace co;

#if !USE_ROUTINE_SYNC
TEST(RoutineSyncLinkedList, simple) {}
#else

struct A : public libgo::LinkedNode
{
    int v = 0;

    A() = default;
    explicit A(int _v) : v(_v) {}
};

TEST(RoutineSyncLinkedList, simple) 
{
    libgo::LinkedList li;
    EXPECT_EQ(li.front(), nullptr);

    const int c = 100;

    for (int i = 0; i < c; ++i) {
        li.push(new A(i));
    }

    for (int i = 0; i < c; ++i) {
        A* a = (A*)li.front();
        EXPECT_EQ(a->v, i);
        li.unlink(a);
        delete a;
    }
    
    EXPECT_EQ(li.front(), nullptr);
}


TEST(RoutineSyncLinkedList, random_erase) 
{
    libgo::LinkedList li;
    EXPECT_EQ(li.front(), nullptr);

    const int c = 100;

    std::vector<A*> vec;

    for (int i = 0; i < c; ++i) {
        vec.push_back(new A(i));
        li.push(vec.back());
    }

    for (int i = 0; i < 10; ++i) {
        A* a = vec.back();
        vec.pop_back();
        li.unlink(a);
        delete a;
    }

    std::random_shuffle(vec.begin(), vec.end());

    for (A* a : vec) {
        li.unlink(a);
        delete a;
    }
    
    EXPECT_EQ(li.front(), nullptr);
}
#endif