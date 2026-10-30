#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

// Sliding-window event counter.
//   add(ts)    : record an event at ts (timestamps assumed non-decreasing)
//   count(now) : #events with ts > now - window_ns  (now is non-decreasing)
//
// Backed by a power-of-two ring buffer of timestamps (FIFO).
// Old events are expired lazily in count(). Each timestamp is pushed once and
// popped at most once  =>  amortized O(1) per add/count. Buffer doubles when
// full (amortized O(1) as well) and never shrinks, so steady state is
// allocation-free.
struct RollingCounter {
    explicit RollingCounter(uint64_t window_ns, uint64_t initial_cap = 1024)
        : window_(window_ns) {
        cap_ = 16;
        while (cap_ < initial_cap) cap_ <<= 1;
        mask_ = cap_ - 1;
        buf_ = static_cast<uint64_t*>(std::malloc(cap_ * sizeof(uint64_t)));
        if (!buf_) throw std::bad_alloc();
    }

    ~RollingCounter() { std::free(buf_); }
    RollingCounter(const RollingCounter&) = delete;
    RollingCounter& operator=(const RollingCounter&) = delete;

    // record an event
    inline void add(uint64_t ts_ns) {
        if (__builtin_expect(tail_ - head_ == cap_, 0)) grow();
        buf_[tail_ & mask_] = ts_ns;
        ++tail_;
    }

    // events with ts > now - window_ns
    inline uint64_t count(uint64_t now_ns) {
        // If now < window, (now - window) is "negative": nothing can expire.
        if (now_ns >= window_) {
            const uint64_t cutoff = now_ns - window_;   // expire ts <= cutoff
            while (head_ != tail_ && buf_[head_ & mask_] <= cutoff) ++head_;
        }
        return tail_ - head_;
    }

private:
    void grow() {
        const uint64_t n = tail_ - head_;   // == cap_
        const uint64_t new_cap = cap_ << 1;
        uint64_t* nb = static_cast<uint64_t*>(std::malloc(new_cap * sizeof(uint64_t)));
        if (!nb) throw std::bad_alloc();
        // copy in logical order (at most two contiguous chunks)
        const uint64_t h = head_ & mask_;
        const uint64_t first = cap_ - h;
        std::memcpy(nb, buf_ + h, first * sizeof(uint64_t));
        std::memcpy(nb + first, buf_, (n - first) * sizeof(uint64_t));
        std::free(buf_);
        buf_ = nb;
        cap_ = new_cap;
        mask_ = new_cap - 1;
        head_ = 0;
        tail_ = n;
    }

    uint64_t* buf_ = nullptr;
    uint64_t  cap_ = 0;
    uint64_t  mask_ = 0;
    uint64_t  head_ = 0;   // monotonically increasing logical indices;
    uint64_t  tail_ = 0;   // physical slot = index & mask_
    uint64_t  window_;
};
