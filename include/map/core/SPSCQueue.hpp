#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace map {

    /**
     * Single-producer / single-consumer bounded ring buffer.
     * Lock-free using atomics. Capacity must be > 1.
     *
     * Producer: push()
     * Consumer: pop()
     */
    template <typename T, std::size_t Capacity>
    class SPSCQueue {
        static_assert(Capacity > 1, "Capacity must be > 1");

    public:
        SPSCQueue()
            : head_(0), tail_(0) {}

        // Non-copyable
        SPSCQueue(const SPSCQueue&)            = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;

        /**
         * Enqueue one item (single producer).
         * Returns false if the buffer is full.
         */
        bool push(const T& value) {
            std::size_t head = head_.load(std::memory_order_relaxed);
            std::size_t next = increment(head);

            // If next == tail, buffer is full.
            if (next == tail_.load(std::memory_order_acquire)) {
                return false;
            }

            buffer_[head] = value;
            head_.store(next, std::memory_order_release);
            return true;
        }

        /**
         * Dequeue one item (single consumer).
         * Returns false if the buffer is empty.
         */
        bool pop(T& out) {
            std::size_t tail = tail_.load(std::memory_order_relaxed);

            // If tail == head, buffer is empty.
            if (tail == head_.load(std::memory_order_acquire)) {
                return false;
            }

            out = buffer_[tail];
            tail_.store(increment(tail), std::memory_order_release);
            return true;
        }

    private:
        static constexpr std::size_t increment(std::size_t idx) {
            return (idx + 1) % Capacity;
        }

        std::array<T, Capacity> buffer_{};
        std::atomic<std::size_t> head_;
        std::atomic<std::size_t> tail_;
    };

} // namespace map
