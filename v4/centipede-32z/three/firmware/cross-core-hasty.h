#ifndef _CROSS_CORE_FIFO_HASTY_H_
#define _CROSS_CORE_FIFO_HASTY_H_

// Simplified single-threaded FIFO (no memory barriers or atomics).
// Based on cross-core.h but stripped for single-core use.

template <typename T, uint32_t Size>
class HastyCrossCoreFIFO {
  static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

 public:
  HastyCrossCoreFIFO() : head(0), tail(0) {}

  FORCE_INLINE bool push(const T& item) {
    uint32_t next_head = (head + 1) & (Size - 1);

    if (next_head == tail) {
      return false;  // Buffer full
    }

    data[head] = item;
    head = next_head;
    return true;
  }

  FORCE_INLINE bool pop(T& item) {
    if (tail == head) {
      return false;  // Buffer empty
    }

    item = data[tail];
    tail = (tail + 1) & (Size - 1);
    return true;
  }

  FORCE_INLINE uint32_t size() const { return (head - tail) & (Size - 1); }

 private:
  std::array<T, Size> data;
  uint32_t head;
  uint32_t tail;
};

#endif
