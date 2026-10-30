#include "../rolling_counter.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>
#include <random>

struct Ref {  // naive reference
    uint64_t w; std::deque<uint64_t> d;
    void add(uint64_t t) { d.push_back(t); }
    uint64_t count(uint64_t now) {
        uint64_t c = 0;
        for (auto t : d) if (now < w || t > now - w) ++c;
        return c;
    }
};

int main() {
    // edge cases
    { RollingCounter rc(100);
      assert(rc.count(0) == 0);
      rc.add(0); assert(rc.count(50) == 1);
      assert(rc.count(99) == 1);
      assert(rc.count(100) == 0);          // ts=0 > 0 ? no -> expired
      rc.add(150); rc.add(150);
      assert(rc.count(249) == 2);
      assert(rc.count(250) == 0); }

    // randomized vs reference (includes growth past initial cap)
    std::mt19937_64 rng(42);
    for (int trial = 0; trial < 200; ++trial) {
        uint64_t w = 1 + rng() % 1000;
        RollingCounter rc(w, 16); Ref ref{w, {}};
        uint64_t t = rng() % 50;
        for (int i = 0; i < 5000; ++i) {
            t += rng() % 20;
            if (rng() & 1) { rc.add(t); ref.add(t); }
            else assert(rc.count(t) == ref.count(t));
        }
    }
    std::puts("correctness OK");

    // benchmark: interleaved add/count
    const uint64_t N = 50'000'000, W = 1'000'000;
    RollingCounter rc(W);
    uint64_t t = 0, sink = 0;
    auto s = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < N; ++i) {
        t += 37;
        rc.add(t);
        sink += rc.count(t);
    }
    auto e = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(e - s).count();
    std::printf("%.2f ns/op (add+count pair = 2 ops), sink=%llu\n",
                ns / (2.0 * N), (unsigned long long)sink);
}
