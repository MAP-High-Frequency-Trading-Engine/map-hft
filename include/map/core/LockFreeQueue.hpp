#pragma once

#include <atomic>
#include <array>
#include <optional>

// Single-producer, single-consumer ring buffer.
// Lock-free using atomic indices; capacity must be power of two for masking.
namespace map {

template <typename T, std::size_t CapacityPow2>
class LockFreeQueue {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0,
                  "Capacity must be power of two");
public:
    LockFreeQueue() : head_(0), tail_(0) {}

    bool push(const T& v) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next = (head + 1) & mask();
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer_[tail];
        tail_.store((tail + 1) & mask(), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    constexpr std::size_t mask() const { return CapacityPow2 - 1; }

    std::array<T, CapacityPow2> buffer_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
};

} // namespace map
