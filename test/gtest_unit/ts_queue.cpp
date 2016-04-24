#include "gtest/gtest.h"
#include <vector>
#include <list>
#include <atomic>
#include <boost/thread.hpp>
#include "gtest_exit.h"
#include <chrono>
#define private public
#include "coroutine.h"
#include "ts_queue.h"
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
    q.push(std::move(slist));
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
    q.push(std::move(slist));
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
    q.push(std::move(slist));
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
    q.push(std::move(slist));
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
    q.push(std::move(slist));
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
    q.push(std::move(slist));
    EXPECT_EQ(id_vec.front(), first);

    q.push(std::move(slist));
    q.pop_all();
    q.pop_all();
    q.pop_all();
}

TEST(TSQueue, Erase) {
}

TEST(TSQueue, LFLock) {
}

TEST(TSSkipQueueT, pushpop)
{
    QueueElem::s_id = 0;
    int n = 10000;
    TSSkipQueue<QueueElem, true, 64> q;

    // push
    q.push(new QueueElem);
    EXPECT_TRUE(q.skip_layer_.indexs_.empty());
    GTEST_ASSERT_EQ(q.size(), 1);
    EXPECT_EQ(q.skip_layer_.head_offset_, 0);
    EXPECT_EQ(q.skip_layer_.tail_offset_, 1);

    for (int i = 1; i <= 62; ++i)
    {
        q.push(new QueueElem);
        EXPECT_TRUE(q.skip_layer_.indexs_.empty());
        GTEST_ASSERT_EQ(q.size(), i + 1);
        EXPECT_EQ(q.skip_layer_.head_offset_, 0);
        EXPECT_EQ(q.skip_layer_.tail_offset_, i + 1);
    }

    q.push(new QueueElem);
    EXPECT_EQ(q.skip_layer_.indexs_.size(), 1);
    GTEST_ASSERT_EQ(q.size(), 64);
    EXPECT_EQ(q.skip_layer_.head_offset_, 0);
    EXPECT_EQ(q.skip_layer_.tail_offset_, 0);

    q.push(new QueueElem);
    EXPECT_EQ(q.skip_layer_.indexs_.size(), 1);
    GTEST_ASSERT_EQ(q.size(), 65);
    EXPECT_EQ(q.skip_layer_.head_offset_, 0);
    EXPECT_EQ(q.skip_layer_.tail_offset_, 1);

    for (int i = 66; i <= n; ++i)
    {
        q.push(new QueueElem);
        EXPECT_EQ(q.skip_layer_.indexs_.size(), i / 64);
        GTEST_ASSERT_EQ(q.size(), i);
        EXPECT_EQ(q.skip_layer_.head_offset_, 0);
        EXPECT_EQ(q.skip_layer_.tail_offset_, i % 64);
    }

    // pop
    SList<QueueElem> s = q.pop(1);
    EXPECT_EQ(s.begin()->id_, 1);
    EXPECT_EQ(q.skip_layer_.indexs_.size(), n / 64);
    GTEST_ASSERT_EQ(q.size(), n - 1);
    EXPECT_EQ(q.skip_layer_.head_offset_, 1);
    EXPECT_EQ(q.skip_layer_.tail_offset_, n % 64);

    s = q.pop(62);
    int c = 2;
    for (auto &elem : s)
    {
        EXPECT_EQ(elem.id_, c);
        c++;
    }
    EXPECT_EQ(q.skip_layer_.indexs_.size(), n / 64);
    GTEST_ASSERT_EQ(q.size(), n - 63);
    EXPECT_EQ(q.skip_layer_.head_offset_, 63);
    EXPECT_EQ(q.skip_layer_.tail_offset_, n % 64);

    s = q.pop(1);
    EXPECT_EQ(s.begin()->id_, c);
    c++;
    GTEST_ASSERT_EQ(q.size(), n - 64);
    EXPECT_EQ(q.skip_layer_.indexs_.size(), n / 64 - 1);
    EXPECT_EQ(q.skip_layer_.head_offset_, 0);
    EXPECT_EQ(q.skip_layer_.tail_offset_, n % 64);

    s = q.pop(n);
    EXPECT_EQ(s.size(), n - 64);
    for (auto &elem : s)
    {
        EXPECT_EQ(elem.id_, c);
        c++;
    }
    EXPECT_TRUE(q.skip_layer_.indexs_.empty());
    GTEST_ASSERT_EQ(q.size(), 0);
    EXPECT_EQ(q.skip_layer_.head_offset_, 0);
    EXPECT_EQ(q.skip_layer_.tail_offset_, 0);

    for (int i = 0; i < 100; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            QueueElem *p = new QueueElem(j);
            GTEST_ASSERT_EQ(p->id_, j);
            q.push(p);
        }
        GTEST_ASSERT_EQ(q.size(), n);
        s = q.pop(-1);
        GTEST_ASSERT_EQ(q.size(), 0);
        int j = 0;
        for (auto &elem : s)
        {
            EXPECT_EQ(elem.id_, j);
            ++j;
        }
    }
}

TEST(TSSkipQueueT, MultipleThread)
{
    QueueElem::s_id = 0;
    int n = 10000;
    int thread_c = 3;
    TSSkipQueue<QueueElem, true, 64> q;

    boost::thread_group tg;
    for (int i = 0; i < thread_c; ++i)
    {
        tg.create_thread([=, &q] {
                    for (int i = 0; i < n; ++i)
                        q.push(new QueueElem);
                });
    }

    tg.create_thread([=, &q] {
                int c = 0;
                for (; c < n * thread_c;) {
                    SList<QueueElem> s = q.pop(1000);
                    c += s.size();
                }
                EXPECT_EQ(c, n * thread_c);
            });
    tg.join_all();
}

TEST(TSSkipQueueT, Bm)
{
    QueueElem::s_id = 0;
    int n = 10000;
    int thread_c = 3;
    TSSkipQueue<QueueElem, true, 64> q;

    boost::thread_group tg;
    for (int i = 0; i < thread_c; ++i)
    {
        tg.create_thread([=, &q] {
                    for (int i = 0; i < n; ++i)
                        q.push(new QueueElem);
                });
    }

    tg.create_thread([=, &q] {
                int c = 0;
                for (; c < n * thread_c;) {
                    SList<QueueElem> s = q.pop(1000);
                    c += s.size();
                }
                EXPECT_EQ(c, n * thread_c);
            });
    tg.join_all();
}

template <class T>
struct CompareQueues : public ::testing::Test
{
    T q;
};

//using ::testing::Types;
//typedef Types<TSSkipQueue<QueueElem, true, 64>, TSQueue<QueueElem, true>> Implementations;
//typedef Types<TSSkipQueue<QueueElem, true, 64>> Implementations;
//typedef Types<int, long> Implementations;
//TYPED_TEST_CASE(CompareQueues, Implementations)

static const int step_dis = 10000;

TEST(CompareQueues, TSSkipQueueTest)
{
    QueueElem::s_id = 0;
    int n = 1000000;
    TSSkipQueue<QueueElem, true, 64> q;

    for (int i = 0; i < n; ++i)
        q.push(new QueueElem);

    boost::thread_group tg;
    for (int i = 0; i < 4; ++i)
    {
        tg.create_thread([&q] {
                    for (;;) {
                        SList<QueueElem> s = q.pop(step_dis);
                        if (s.empty()) break;
                    }
                });
    }
    tg.join_all();
}

TEST(CompareQueues, TSQueueTest)
{
    QueueElem::s_id = 0;
    int n = 1000000;
    TSQueue<QueueElem, true> q;

    for (int i = 0; i < n; ++i)
        q.push(new QueueElem);

    boost::thread_group tg;
    for (int i = 0; i < 4; ++i)
    {
        tg.create_thread([&q] {
                    for (;;) {
                        SList<QueueElem> s = q.pop(step_dis);
                        if (s.empty()) break;
                        usleep(10000);
                    }
                });
    }
    tg.join_all();
}

struct TestRef : public TSQueueHook, public RefObject
{
    static std::atomic<uint32_t> s_id;
    static std::atomic<uint32_t> s_count;
    uint32_t id_;

    TestRef() : id_(++s_id) { ++s_count; }
    explicit TestRef(uint32_t a) : id_(a) { ++s_count; }
    ~TestRef() { --s_count; }
};
std::atomic<uint32_t> TestRef::s_id {0};
std::atomic<uint32_t> TestRef::s_count {0};

TEST(Ref, TsQueueRef)
{
    {
        TestRef * ptr = new TestRef;
        EXPECT_EQ(TestRef::s_count, 1);
        EXPECT_EQ(ptr->reference_, 1);
        TSQueue<TestRef> q;
        q.push(ptr);
        EXPECT_EQ(ptr->reference_, 2);
        TestRef * out = q.pop();
        EXPECT_EQ(ptr->reference_, 1);
        out->DecrementRef();
        EXPECT_EQ(TestRef::s_count, 0);
    }

    {
        TestRef * ptr = new TestRef;
        EXPECT_EQ(TestRef::s_count, 1);
        EXPECT_EQ(ptr->reference_, 1);
        TSQueue<TestRef> q;
        q.push(ptr);
        EXPECT_EQ(ptr->reference_, 2);
        q.erase(ptr);
        EXPECT_EQ(ptr->reference_, 1);
        ptr->DecrementRef();
        EXPECT_EQ(TestRef::s_count, 0);
    }

    {
        const int n = 128;
        TSQueue<TestRef> q;
        for (int i = 0; i < n; ++i)
            q.push(new TestRef);
        EXPECT_EQ(TestRef::s_count, n);
        SList<TestRef> slist = q.pop_all();
        EXPECT_EQ(TestRef::s_count, n);
        for (auto &elem : slist)
            elem.DecrementRef();
        EXPECT_EQ(TestRef::s_count, n);
        slist.clear();
        EXPECT_EQ(TestRef::s_count, 0);
    }

    {
        const int n = 128;
        TSQueue<TestRef> q;
        for (int i = 0; i < n; ++i)
            q.push(new TestRef);
        EXPECT_EQ(TestRef::s_count, n);
        SList<TestRef> slist = q.pop(28);
        EXPECT_EQ(TestRef::s_count, n);
        for (auto &elem : slist)
            elem.DecrementRef();
        EXPECT_EQ(TestRef::s_count, n);
        slist.clear();
        EXPECT_EQ(TestRef::s_count, 100);
        q.push(q.pop_all());
        q.push(q.pop_all());
        q.push(q.pop_all());
        q.push(q.pop_all());
        slist = q.pop_all();
        for (auto &elem : slist)
            elem.DecrementRef();
        EXPECT_EQ(TestRef::s_count, 100);
    }
}

TEST(Ref, TsSkipQueueRef)
{
    {
        const int n = 128;
        TSSkipQueue<TestRef> q;
        for (int i = 0; i < n; ++i)
            q.push(new TestRef);
        EXPECT_EQ(TestRef::s_count, n);
        SList<TestRef> slist = q.pop(128);
        EXPECT_EQ(TestRef::s_count, n);
        for (auto &elem : slist)
            elem.DecrementRef();
        EXPECT_EQ(TestRef::s_count, n);
        slist.clear();
        EXPECT_EQ(TestRef::s_count, 0);
    }

    {
        const int n = 128;
        TSSkipQueue<TestRef> q;
        for (int i = 0; i < n; ++i)
            q.push(new TestRef);
        EXPECT_EQ(TestRef::s_count, n);
        SList<TestRef> slist = q.pop(28);
        EXPECT_EQ(TestRef::s_count, n);
        for (auto &elem : slist)
            elem.DecrementRef();
        EXPECT_EQ(TestRef::s_count, n);
        slist.clear();
        EXPECT_EQ(TestRef::s_count, 100);
        slist = q.pop(100);
        for (auto &elem : slist)
            elem.DecrementRef();
        EXPECT_EQ(TestRef::s_count, 100);
    }
}

