#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <assert.h>
#include "../../libgo/common/lock_free_ring_queue.h"
using namespace std;
using namespace std::chrono;

#define OUT(x) cout << #x << " = " << x << endl
#define O(x) cout << x << endl

struct Timer { Timer() : tp(system_clock::now()) {} virtual ~Timer() { auto dur = system_clock::now() - tp; O("Cost " << duration_cast<milliseconds>(dur).count() << " ms"); } system_clock::time_point tp; };
struct Bench : public Timer { Bench() : val(0) {} virtual ~Bench() { auto dur = system_clock::now() - tp; O("Per op: " << duration_cast<nanoseconds>(dur).count() / std::max(val, 1L) << " ns"); auto perf = (double)val / duration_cast<milliseconds>(dur).count() / 10; if (perf < 1) O("Performance: " << std::setprecision(3) << perf << " w/s"); else O("Performance: " << perf << " w/s"); } Bench& operator++() { ++val; return *this; } Bench& operator++(int) { ++val; return *this; } Bench& add(long v) { val += v; } long val; };

#define IS_UNIT_TEST 1
#define IS_DEBUG 0

struct A
{
    static std::atomic<long> sCount;
    long val_;
    A() : val_(0) {
        ++sCount;
    }
    A(long val) : val_(val) {
        ++sCount;
    }
    A(A const& a) : val_(a.val_) {
        ++sCount;
    }
    A(A && a) : val_(a.val_) {
        a.val_ = 0;
        ++sCount;
    }
    ~A() {
        --sCount;
    }
    A& operator=(A const& a) {
        val_ = a.val_;
    }
    A& operator=(A && a) {
        val_ = a.val_;
        a.val_ = 0;
    }
    operator long() const { return val_; }
};
std::atomic<long> A::sCount{0};

void AssertCount(long c, A*) {
    assert(A::sCount == c);
}

template <typename T>
void AssertCount(long c, T*) {
    (void)c;
}

template <typename T = long>
void test(int cap, int count, int rThreads, int wThreads)
{
    co::LockFreeRingQueue<T> queue(cap);
    std::atomic_int lastCount{0};
    volatile bool done = false;

#if IS_UNIT_TEST
    AssertCount(0, (T*)nullptr);

    std::vector<std::atomic<long>*> check(wThreads, nullptr);
    for (auto & p : check)
        p = new std::atomic<long>{0};
#endif

    std::vector<thread*> tg;

    // read threads
    for (int i = 0; i < rThreads; ++i) {
        thread *t = new thread([&]{
                    T val;
                    while (!done) {
                        co::LockFreeResult result = queue.Pop(val);
                        if (!result.success)
                            continue;

                        long l = val;
                        int threadNumber = l >> 32;
                        int idx = l & 0xffffffff;
#if IS_DEBUG
                        printf("pop thread=%d, idx=%d\n", threadNumber, idx);
#endif
                        if (idx == count) {
                            if (++lastCount == wThreads)
                                done = true;
                        }
#if IS_UNIT_TEST
                        *check[threadNumber] += idx;
#endif
                    }
                });
        tg.push_back(t);
    }

    // write threads
    for (int i = 0; i < wThreads; ++i) {
        thread *t = new thread([&, i]{
                    for (int j = 1; j <= count; ++j) {
                        T val((long)j | (long)i << 32);
                        while (!queue.Push(std::move(val)).success) ;
#if IS_DEBUG
                        printf("push thread=%d, idx=%d\n", i, j);
#endif
                    }
                });
        tg.push_back(t);
    }

    // join
    for (auto pt : tg)
        pt->join();

#if IS_UNIT_TEST
    AssertCount(0, (T*)nullptr);

    long checkTotal = (1 + count) * count / 2;
    for (auto & p : check)
    {
        assert(*p == checkTotal);
        delete p;
    }
#endif
}

int main() {
    {
        Timer t;
        test<A>(10000, 10000, 10, 10);
    }

    {
        Timer t;
        test(10000, 10000, 10, 10);
    }
}
