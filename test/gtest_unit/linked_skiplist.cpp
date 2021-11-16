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
TEST(RoutineSyncLinkedSkipList, simple) {}
#else

typedef libgo::LinkedSkipList<int, int> list_t;

struct A : public list_t::Node
{
    // A() = default;
    explicit A(int _v) {
        key = _v;
        value = _v * 100;
    }
};

TEST(RoutineSyncLinkedSkipList, simple)
{
    list_t li;
    EXPECT_EQ(li.front(), nullptr);

    const int c = 100;

    for (int i = 0; i < c; ++i) {
        A* a = new A(i);
        li.insert(a);
    }

    for (int i = 0; i < c; ++i) {
        A* a = (A*)li.front();
        EXPECT_EQ(a->key, i);
        EXPECT_EQ(a->value, i * 100);
        li.erase(a);
        delete a;
    }
    
    EXPECT_EQ(li.front(), nullptr);
}

TEST(RoutineSyncLinkedSkipList, sort) 
{
    list_t li;
    EXPECT_EQ(li.front(), nullptr);

    const int c = 10000;
    vector<int> vec;
    vec.reserve(c);

    for (int i = 0; i < c; ++i) {
        A* a = new A(rand());
        vec.push_back(a->key);
        li.insert(a);
    }

    std::sort(vec.begin(), vec.end());

    int maxHeight = 0;

    for (int i = 0; i < c; ++i) {
        A* a = (A*)li.front();
        EXPECT_EQ(a->key, vec[i]);
        if (a->height > maxHeight) {
            printf("[%d]flush height=%d\n", i, a->height);
            maxHeight = a->height;
        }
        li.erase(a);
        delete a;
    }
    
    EXPECT_EQ(li.front(), nullptr);
    EXPECT_EQ(li.height(), 1);
}

TEST(RoutineSyncLinkedSkipList, random_erase) 
{
    list_t li;
    EXPECT_EQ(li.front(), nullptr);

    const int c = 100;

    std::vector<A*> vec;

    for (int i = 0; i < c; ++i) {
        vec.push_back(new A(i));
        li.insert(vec.back());
    }

    for (int i = 0; i < 10; ++i) {
        A* a = vec.back();
        vec.pop_back();
        li.erase(a);
        delete a;
    }

    std::random_shuffle(vec.begin(), vec.end());

    for (A* a : vec) {
        li.erase(a);
        delete a;
    }
    
    EXPECT_EQ(li.front(), nullptr);
}
#endif