#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <map>
#include "../../libgo/common/timer.h"
using namespace std;
using namespace std::chrono;

#define OUT(x) cout << #x << " = " << x << endl
#define O(x) cout << x << endl

struct Timer { Timer() : tp(system_clock::now()) {} virtual ~Timer() { auto dur = system_clock::now() - tp; O("Cost " << duration_cast<milliseconds>(dur).count() << " ms"); } system_clock::time_point tp; };
struct Bench : public Timer { Bench() : val(0) {} virtual ~Bench() { stop(); } void stop() { auto dur = system_clock::now() - tp; O("Per op: " << duration_cast<nanoseconds>(dur).count() / std::max(val, 1L) << " ns"); auto perf = (double)val / duration_cast<milliseconds>(dur).count() / 10; if (perf < 1) O("Performance: " << std::setprecision(3) << perf << " w/s"); else O("Performance: " << perf << " w/s"); } Bench& operator++() { ++val; return *this; } Bench& operator++(int) { ++val; return *this; } Bench& add(long v) { val += v; return *this; } long val; };

static const int cThreads = 1;
static const int cVal = 10000000;

long gVal = 0;
Bench *pb = nullptr;

typedef void (*func_t)();
typedef co::Timer<func_t> timer_type;
timer_type timer;

void emptyFunc() {
    if (++gVal == cVal * cThreads) {
        pb->add(cVal * cThreads);
        delete pb;
        exit(0);
    }
}

void show() {
    for (;;) {
        O("----------------------------------");
        OUT(gVal);
        OUT(timer.point_.p64);
        for (int i = 0; i < 8; i++) {
            std::map<int, int> m;
            for (int j = 0; j < 256; j++) {
                auto & slot = timer.slots_[i][j];
                if (slot.size() > 0) {
                    m[j] = slot.size();
                }
            }
            cout << i << ": p=" << (int)timer.point_.p8[i];
            for (auto & kv : m) {
                cout << " {" << kv.first << ": " << kv.second << "}";
            }
            cout << endl;
        }
        cout << "Complete: " << timer.completeSlot_.size() << endl;
//        OUT(timer_type::Element::getCount());
        OUT(timer.GetPoolSize());
        sleep(1);
    }
}

std::vector<timer_type::TimerId> g_ids(cVal * cThreads);
void insert(int nThread) {
    for (int i = 0; i < cVal; ++i) {
        timer.StartTimer(milliseconds(1), &emptyFunc);
//        g_ids[i + cVal * nThread] = timer.StartTimer(milliseconds(1), &emptyFunc);
    }
}

void insertWithStop(int nThread) {
    for (int i = 0; i < cVal; ++i)
        g_ids[i + cVal * nThread] = timer.StartTimer(milliseconds(i + nThread), &emptyFunc);
}
void stop(int nThread) {
    for (int i = 0; i < cVal; ++i) {
        auto & id = g_ids[i + cVal * nThread];
        id.StopTimer();
    }
}

int main() {
    thread(&co::FastSteadyClock::ThreadRun).detach();
    usleep(300 * 1000);
    timer.SetPoolSize(cVal * cThreads, cVal * cThreads);
//    thread(&show).detach();

    thread([]{
            for (;;) {
//                OUT(timer_type::Element::getCount());
//                OUT(timer.GetPoolSize());
                sleep(1);
            }
        }).detach();

//    OUT(timer.GetPoolSize());

    {
        O("---------- StartTimer ----------");
        Bench b;
        b.add(cVal * cThreads);
        vector<thread*> tg;
        for (int i = 0; i < cThreads; ++i) {
            tg.push_back(new thread([=]{ insertWithStop(i); } ));
        }
        for (auto t : tg)
            t->join();
    }

//    OUT(timer.GetPoolSize());

    {
        O("---------- StopTimer ----------");
        Bench b;
        b.add(cVal * cThreads);
        vector<thread*> tg;
        for (int i = 0; i < cThreads; ++i) {
            tg.push_back(new thread([=]{ stop(i); } ));
        }
        for (auto t : tg)
            t->join();
    }

//    OUT(timer_type::Element::getCount());
//    OUT(timer.GetPoolSize());

//    {
//        O("---------- StartTimer ----------");
//        Bench b;
//        b.add(cVal * cThreads);
//        vector<thread*> tg;
//        for (int i = 0; i < cThreads; ++i) {
//            tg.push_back(new thread([=]{ insert(i); } ));
//        }
//        for (auto t : tg)
//            t->join();
//    }

    {
        O("---------- StartTimer(1 Thread) ----------");
        Bench b;
        b.add(cVal * cThreads);
        for (int i = 0; i < cThreads; ++i) {
            insert(i);
        }
    }

//    OUT(timer_type::Element::getCount());
//    OUT(timer.GetPoolSize());

    O("---------- Run ----------");
    pb = new Bench;
    timer.ThreadRun();
}

