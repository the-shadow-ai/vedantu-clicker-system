#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>


namespace svs {

/// Cache-line padded MPSC (multi-producer, single-consumer) lock-free ring
/// buffer. Capacity must be a power of two. Producers are thread-safe; only ONE
/// consumer thread may call pop().
template <typename T, size_t Capacity = 65536> class LockFreeQueue {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "LockFreeQueue capacity must be a power of two");

  // Pad to 64 bytes to avoid false sharing on x86/x64
  static constexpr size_t CACHE_LINE = 64;
  static constexpr size_t MASK = Capacity - 1;

  struct alignas(CACHE_LINE) Slot {
    std::atomic<uint64_t> seq{0};
    T data{};
  };

  alignas(CACHE_LINE) std::array<Slot, Capacity> slots_;
  alignas(CACHE_LINE) std::atomic<uint64_t> head_{0}; // producers claim
  alignas(CACHE_LINE) std::atomic<uint64_t> tail_{0}; // consumer reads

public:
  LockFreeQueue() {
    for (size_t i = 0; i < Capacity; ++i)
      slots_[i].seq.store(i, std::memory_order_relaxed);
  }

  /// Try to push one item. Returns true on success, false if full.
  bool push(T value) noexcept {
    uint64_t pos = head_.load(std::memory_order_relaxed);
    for (;;) {
      Slot &slot = slots_[pos & MASK];
      uint64_t seq = slot.seq.load(std::memory_order_acquire);
      int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);

      if (diff == 0) {
        // Slot is free — try to claim it
        if (head_.compare_exchange_weak(pos, pos + 1,
                                        std::memory_order_relaxed)) {
          slot.data = std::move(value);
          slot.seq.store(pos + 1, std::memory_order_release);
          return true;
        }
      } else if (diff < 0) {
        return false; // queue is full
      } else {
        pos = head_.load(std::memory_order_relaxed);
      }
    }
  }

  /// Try to pop one item. Returns empty optional if queue is empty.
  /// Must be called from a SINGLE consumer thread.
  std::optional<T> pop() noexcept {
    uint64_t pos = tail_.load(std::memory_order_relaxed);
    Slot &slot = slots_[pos & MASK];
    uint64_t seq = slot.seq.load(std::memory_order_acquire);
    int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos + 1);

    if (diff != 0)
      return std::nullopt; // empty

    tail_.store(pos + 1, std::memory_order_relaxed);
    T value = std::move(slot.data);
    slot.seq.store(pos + Capacity, std::memory_order_release);
    return value;
  }

  [[nodiscard]] bool empty() const noexcept {
    uint64_t tail = tail_.load(std::memory_order_relaxed);
    uint64_t head = head_.load(std::memory_order_relaxed);
    return head == tail;
  }

  [[nodiscard]] size_t approx_size() const noexcept {
    uint64_t head = head_.load(std::memory_order_relaxed);
    uint64_t tail = tail_.load(std::memory_order_relaxed);
    return (head >= tail) ? (head - tail) : 0;
  }
};

} // namespace svs
