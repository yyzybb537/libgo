#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <libgo/libgo.h>
#include <atomic>
#include <chrono>
#include <iomanip>
using namespace std;
using namespace std::chrono;

#define OUT(x) cout << #x << " = " << x << endl
#define O(x) cout << x << endl

struct Timer { Timer() : tp(system_clock::now()) {} virtual ~Timer() { auto dur = system_clock::now() - tp; O("Cost " << duration_cast<milliseconds>(dur).count() << " ms"); } system_clock::time_point tp; };
struct Bench : public Timer { Bench() : val(0) {} virtual ~Bench() { stop(); } void stop() { auto dur = system_clock::now() - tp; O("Per op: " << duration_cast<nanoseconds>(dur).count() / std::max(val, 1L) << " ns"); auto perf = (double)val / duration_cast<milliseconds>(dur).count() / 10; if (perf < 1) O("Performance: " << std::setprecision(3) << perf << " w/s"); else O("Performance: " << perf << " w/s"); } Bench& operator++() { ++val; return *this; } Bench& operator++(int) { ++val; return *this; } Bench& add(long v) { val += v; return *this; } long val; };

const int cSwitch = 100000000;

co::Task *gTask = nullptr;

void foo() {
    {
        Bench b;
        b.add(cSwitch);
        for (int j = 0; j < cSwitch; ++j) {
            gTask->SwapOut();
        }
        gTask->state_ = co::TaskState::done;
    }
    gTask->SwapOut();
}

int main()
{
    gTask = new co::Task(&foo, 128 * 1024);
    while (gTask->state_ != co::TaskState::done) {
        gTask->SwapIn();
    }
    printf("Done\n");
}
