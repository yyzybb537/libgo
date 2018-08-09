#include "gtest/gtest.h"
#include <vector>
#include <list>
#include <atomic>
#include <boost/thread.hpp>
#include "gtest_exit.h"
#include <chrono>
#define private public
#include "coroutine.h"
#include "libgo/common/ts_queue.h"
using namespace co;
using namespace std;
using namespace std::chrono;

struct QueueElem : public TSQueueHook
{
    static std::atomic<uint32_t> s_id;
    uint32_t id_;

    QueueElem() : id_(++s_id) {}
    explicit QueueElem(uint32_t a) : id_(a) {}
};
std::atomic<uint32_t> QueueElem::s_id {0};

TEST(TSQueue, DefaultContructor) {
    TSQueue<QueueElem> tsq; 
    EXPECT_TRUE(tsq.empty());
    EXPECT_EQ(NULL, tsq.pop());
}

TEST(TSQueue, benchmark) {
    TSQueue<QueueElem> q; 
    for (int i = 0; i < 1000; ++i)
        q.push(new QueueElem);

    int c = 100000;
    auto s = system_clock::now();
    for (int i = 0; i < c; ++i)
    {
        SList<QueueElem> slist(q.pop_all());
        auto it = slist.begin();
        for (; it != slist.end(); ) {
            QueueElem& p = *it++;
            (void)p;
        }
        q.push(std::move(slist));
    }
    auto e = system_clock::now();
    cout << "pop_all cost " << duration_cast<milliseconds>(e - s).count() << " ms" << endl;

    s = system_clock::now();
    for (int i = 0; i < c; ++i)
    {
        QueueElem* first = (QueueElem*)q.head_->next;
        while (first) {
            QueueElem& p = *first;
            (void)p;
            first = (QueueElem*)first->next;
        }
    }
    e = system_clock::now();
    cout << "pop cost " << duration_cast<milliseconds>(e - s).count() << " ms" << endl;
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
    EXPECT_EQ(slist.size(), 0u);
    EXPECT_TRUE(slist.empty());
    uint32_t first = 1;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop_front(1);
    EXPECT_EQ(slist.size(), 1u);
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

    slist = q.pop_front(1);
    EXPECT_EQ(slist.size(), 1u);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(std::move(slist));
    ++first;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop_front(2);
    EXPECT_EQ(slist.size(), 2u);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(std::move(slist));
    first += 2;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop_front(3);
    EXPECT_EQ(slist.size(), 3u);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(std::move(slist));
    first += 3;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop_front(94);
    EXPECT_EQ(slist.size(), 94u);
    EXPECT_FALSE(slist.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(std::move(slist));
    first += 94;
    first %= 100;
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop_front(100);
    EXPECT_EQ(slist.size(), 100u);
    EXPECT_FALSE(slist.empty());
    EXPECT_TRUE(q.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(std::move(slist));
    EXPECT_EQ(id_vec.front(), first);

    slist = q.pop_front(101);
    EXPECT_EQ(slist.size(), 100u);
    EXPECT_FALSE(slist.empty());
    EXPECT_TRUE(q.empty());
    for (auto& elem:slist) {
        uint32_t bak_id = id_vec.front();
        EXPECT_EQ(elem.id_, bak_id);
        id_vec.pop_front();
        id_vec.push_back(bak_id);
    }
    q.push(std::move(slist));
    EXPECT_EQ(id_vec.front(), first);

    q.push(std::move(slist));
    q.pop_all().stealed();
    q.pop_all();
    q.pop_all();
}

TEST(TSQueue, Erase) {
}

TEST(TSQueue, LFLock) {
}
