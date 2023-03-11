
#include "coroutine.h"
#include <benchmark/benchmark.h>
#include <stdio.h>
#include <thread>

struct C {
  C() { std::cout << __PRETTY_FUNCTION__ << '\n'; }

  C(const C &) { std::cout << __PRETTY_FUNCTION__ << '\n'; }
  C(const C &&) { std::cout << __PRETTY_FUNCTION__ << '\n'; }
  C &operator=(const C &) {
    std::cout << __PRETTY_FUNCTION__ << '\n';
    return *this;
  }
  C &operator=(const C &&) {
    std::cout << __PRETTY_FUNCTION__ << '\n';
    return *this;
  }
  ~C() { std::cout << __PRETTY_FUNCTION__ << '\n'; }
  int i = 100;
};

static void BenchCreateEmpty(benchmark::State &state) {
  for (auto _ : state) {
    std::vector<char> vi(state.range(0));
    go[]{

    };
    benchmark::DoNotOptimize(vi.size());
  }
}

BENCHMARK(BenchCreateEmpty)->Range(1, 8192);

static void BenchCreateCopy(benchmark::State &state) {
  for (auto _ : state) {
    std::vector<char> vi(state.range(0));
    go[vi](){

    };
  }
}

BENCHMARK(BenchCreateCopy)->Range(1, 8192);
static void BenchCreateMove(benchmark::State &state) {
  for (auto _ : state) {
    std::vector<char> vi(state.range(0));
    go[vi = std::move(vi)](){

    };
  }
}

BENCHMARK(BenchCreateMove)->Range(1, 8192);

int main(int argc, char **argv) {
  C c;
  go [c = std::move(c)] {};
  std::thread t([] { co_sched.Start(); });
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  co_sched.Stop();
  t.join();
  return 0;
}