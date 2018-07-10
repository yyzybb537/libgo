#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include "../../libgo/common/clock.h"
using namespace std;
using namespace std::chrono;

#define OUT(x) cout << #x << " = " << x << endl
#define O(x) cout << x << endl

struct Timer { Timer() : tp(system_clock::now()) {} virtual ~Timer() { auto dur = system_clock::now() - tp; O("Cost " << duration_cast<milliseconds>(dur).count() << " ms"); } system_clock::time_point tp; };
struct Bench : public Timer { Bench() : val(0) {} virtual ~Bench() { auto dur = system_clock::now() - tp; O("Per op: " << duration_cast<nanoseconds>(dur).count() / std::max(val, 1L) << " ns"); auto perf = (double)val / duration_cast<milliseconds>(dur).count() / 10; if (perf < 1) O("Performance: " << std::setprecision(3) << perf << " w/s"); else O("Performance: " << perf << " w/s"); } Bench& operator++() { ++val; return *this; } Bench& operator++(int) { ++val; return *this; } Bench& add(long v) { val += v; } long val; };

int main() {
    thread t(&co::FastSteadyClock::ThreadRun);
    t.detach();
    sleep(1);

    {
        O("---------- FastSteadyClock ----------");
        Bench b;
        for (int i = 0; i < 10000000; ++i, ++b)
            co::FastSteadyClock::now();
    }

    {
        O("---------- std::steady_clock ----------");
        Bench b;
        for (int i = 0; i < 10000000; ++i, ++b)
            steady_clock::now();
    }

}
