#include "ts_queue.h"
#include "gtest/gtest.h"
#include <vector>
#include <list>
#include <atomic>
using namespace co;

struct QueueElem : public TSQueueHook
{
    static std::atomic<uint32_t> s_id;
    uint32_t id_;

    QueueElem() : id_(++s_id) {}
};
std::atomic<uint32_t> QueueElem::s_id {0};

TEST(TSQueue, DefaultContructor) {
    TSQueue<QueueElem> tsq; 
    EXPECT_TRUE(tsq.empty());
    EXPECT_EQ(NULL, tsq.pop());
}

TEST(TSQueue, PushPopOne) {
    TSQueue<QueueElem> q; 

    for (int i = 0; i < 10; ++i)
    {
        QueueElem* origin_e = new QueueElem;
        q.push(origin_e);
        QueueElem* e = q.pop();
        EXPECT_EQ(e, origin_e);
        EXPECT_TRUE(q.empty());
        delete origin_e;
    }

    EXPECT_TRUE(q.empty());
    std::vector<uint32_t> id_vec;
    for (int i = 0; i < 10; ++i)
    {
        QueueElem* e = new QueueElem;
        id_vec.push_back(e->id_);
        q.push(e);
        EXPECT_FALSE(q.empty());
    }
    for (int i = 0; i < 10; ++i)
    {
        QueueElem* e = q.pop();
        EXPECT_TRUE(e != NULL);
        EXPECT_EQ(e->id_, id_vec[i]);
        delete e;
    }
    EXPECT_TRUE(q.empty());
}

TEST(TSQueue, PushPopMulti) {
    TSQueue<QueueElem> q; 

    QueueElem::s_id = 0;
    std::list<uint32_t> id_vec;
    for (int i = 0; i < 100; ++i)
    {
        QueueElem* e = new QueueElem;
        id_vec.push_back(e->id_);
        q.push(e);
        EXPECT_FALSE(q.empty());
    }

    SList<QueueElem> slist;
    EXPECT_EQ(slist.size(), 0);
    EXPECT_TRUE(slist.empty());
    uint32_t first = 1;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(1);
    EXPECT_EQ(slist.size(), 1);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    QueueElem* e = &*slist.begin();
    slist.erase(slist.begin());
    q.push(e);
    ++first;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(1);
    EXPECT_EQ(slist.size(), 1);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(slist);
    ++first;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(2);
    EXPECT_EQ(slist.size(), 2);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(slist);
    first += 2;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(3);
    EXPECT_EQ(slist.size(), 3);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(slist);
    first += 3;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(94);
    EXPECT_EQ(slist.size(), 94);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(slist);
    first += 94;
    first %= 100;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(100);
    EXPECT_EQ(slist.size(), 100);
    EXPECT_FALSE(slist.empty());
    EXPECT_TRUE(q.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(slist);
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop(101);
    EXPECT_EQ(slist.size(), 100);
    EXPECT_FALSE(slist.empty());
    EXPECT_TRUE(q.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(slist);
    EXPECT_EQ(id_vec.front(), first);
}

TEST(TSQueue, Erase) {
}

TEST(TSQueue, LFLock) {
}

