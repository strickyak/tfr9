#ifndef _CROSS_CORE_FIFO_H_
#define _CROSS_CORE_FIFO_H_

// Written by Google Gemini 3 Pro.
// Seems to work great.

template <typename T, uint32_t Size>
class CrossCoreFIFO {
  static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

 public:
  CrossCoreFIFO() : head(0), tail(0) {}

  // Called by the Producer Core
  FORCE_INLINE bool push(const T& item) {
    uint32_t next_head =
        (head.load(std::memory_order_relaxed) + 1) & (Size - 1);

    if (next_head == tail.load(std::memory_order_acquire)) {
      return false;  // Buffer full
    }

    data[head.load(std::memory_order_relaxed)] = item;

    // Ensure data is written before head is updated
    head.store(next_head, std::memory_order_release);
    return true;
  }

  // Called by the Consumer Core
  FORCE_INLINE bool pop(T& item) {
    uint32_t current_tail = tail.load(std::memory_order_relaxed);

    if (current_tail == head.load(std::memory_order_acquire)) {
      return false;  // Buffer empty
    }

    item = data[current_tail];

    // Ensure data is read before tail is updated
    tail.store((current_tail + 1) & (Size - 1), std::memory_order_release);
    return true;
  }

  FORCE_INLINE uint32_t size() const {
    return (head.load(std::memory_order_acquire) -
            tail.load(std::memory_order_acquire)) &
           (Size - 1);
  }

 private:
  std::array<T, Size> data;
  std::atomic<uint32_t> head;  // Written by Producer
  std::atomic<uint32_t> tail;  // Written by Consumer
};

#endif
