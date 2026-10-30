// Latency benchmark for RollingCounter: ns/op + p50 / p99 / p99.9
//
// Build:  g++ -O2 -std=c++17 bench/latency_bench.cpp -o lb && ./lb
//
// A single add/count takes ~1 ns, which is far below the resolution of the
// OS clock (Apple Silicon: ~41.7 ns per tick). So we time *batches* of
// BATCH ops and report per-op latency = batch_time / BATCH. Percentiles are
// taken over those per-op batch averages. Timer overhead is measured and
// subtracted.
#include "../rolling_counter.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using clk = std::chrono::steady_clock;

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               clk::now().time_since_epoch()).count();
}

static double pct(std::vector<double>& v, double p) {
    size_t k = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

int main(int argc, char** argv) {
    const int BATCH      = argc > 1 ? std::atoi(argv[1]) : 64;  // ops per sample
    const int SAMPLES    = 1'000'000;
    const uint64_t W     = 1'000'000;   // 1 ms window
    const int WARMUP     = 100'000;

    // Pre-generate the event stream so RNG cost is not timed.
    // Inter-arrival gaps ~ exponential(mean 37 ns), with occasional idle gaps
    // (0.1%) of ~2 ms that force a large lazy expiry on the next count().
    const size_t TOTAL_PAIRS = (size_t)(SAMPLES + WARMUP) * BATCH / 2 + 1;
    std::vector<uint64_t> ts(TOTAL_PAIRS);
    std::mt19937_64 rng(12345);
    std::exponential_distribution<double> gap(1.0 / 37.0);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    uint64_t t = W;
    for (auto& x : ts) {
        t += (u(rng) < 0.001) ? 2'000'000 : (uint64_t)gap(rng) + 1;
        x = t;
    }

    // Timer overhead (median of back-to-back reads).
    std::vector<double> ovh(100'000);
    for (auto& o : ovh) { uint64_t a = now_ns(); uint64_t b = now_ns(); o = double(b - a); }
    const double timer_ovh = pct(ovh, 0.5);

    RollingCounter rc(W);
    std::vector<double> lat;
    lat.reserve(SAMPLES);
    size_t idx = 0;
    volatile uint64_t sink = 0;
    const int PAIRS = BATCH / 2;

    uint64_t total_ns = 0;
    for (int s = 0; s < SAMPLES + WARMUP; ++s) {
        uint64_t a = now_ns();
        uint64_t acc = 0;
        for (int i = 0; i < PAIRS; ++i) {
            uint64_t x = ts[idx++];
            rc.add(x);
            acc += rc.count(x);
        }
        uint64_t b = now_ns();
        sink = sink + acc;
        if (s >= WARMUP) {
            double d = double(b - a) - timer_ovh;
            if (d < 0) d = 0;
            total_ns += (uint64_t)d;
            lat.push_back(d / BATCH);
        }
    }

    const double mean = double(total_ns) / ((double)SAMPLES * BATCH);
    std::printf("RollingCounter latency (window=%llu ns, batch=%d ops, samples=%d)\n",
                (unsigned long long)W, BATCH, SAMPLES);
    std::printf("timer overhead : %.1f ns (subtracted)\n", timer_ovh);
    std::printf("ns/op (mean)   : %.2f\n", mean);
    std::printf("p50            : %.2f ns/op\n", pct(lat, 0.50));
    std::printf("p99            : %.2f ns/op\n", pct(lat, 0.99));
    std::printf("p99.9          : %.2f ns/op\n", pct(lat, 0.999));
    std::printf("max            : %.2f ns/op\n", *std::max_element(lat.begin(), lat.end()));
    (void)sink;
}
