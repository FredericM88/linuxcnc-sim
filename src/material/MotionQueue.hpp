#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace cnc {
// Unbounded single-producer/single-consumer FIFO. No worker mutex, capacity wait,
// overwrite or drop. Allocation failure propagates to the producer as fatal.
// Dummy nodes are reclaimed by the consumer only after the producer released next.
template<class T> class MotionQueue {
    struct Node { T value{}; std::atomic<Node*> next{nullptr}; };
public:
    MotionQueue() : head_(new Node), tail_(head_) {}
    ~MotionQueue() { while (head_) { auto* next = head_->next.load(); delete head_; head_ = next; } }
    MotionQueue(const MotionQueue&) = delete;
    MotionQueue& operator=(const MotionQueue&) = delete;
    void push(T value) {
        auto node = std::make_unique<Node>();
        node->value = std::move(value);
        // Counters precede visibility, preventing consumer count from overtaking producer.
        const auto produced = produced_.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto depth = produced - consumed_.load(std::memory_order_acquire);
        if (depth > maximum_.load(std::memory_order_relaxed)) maximum_.store(depth, std::memory_order_relaxed);
        auto* next = node.release();
        tail_->next.store(next, std::memory_order_release);
        tail_ = next;
    }
    bool pop(T& value) {
        auto* next = head_->next.load(std::memory_order_acquire);
        if (!next) return false;
        value = std::move(next->value);
        delete head_; head_ = next;
        consumed_.fetch_add(1, std::memory_order_release);
        return true;
    }
    std::uint64_t depth() const {
        const auto consumed = consumed_.load(std::memory_order_acquire);
        const auto produced = produced_.load(std::memory_order_acquire);
        return produced - consumed;
    }
    std::uint64_t maximum() const { return maximum_.load(std::memory_order_relaxed); }
private:
    Node* head_; // Consumer only.
    Node* tail_; // Producer only.
    std::atomic<std::uint64_t> produced_{}, consumed_{}, maximum_{};
};
} // namespace cnc
