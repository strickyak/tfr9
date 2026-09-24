#ifndef TURBOLAB_CROSS_CORE_FIFO_H_
#define TURBOLAB_CROSS_CORE_FIFO_H_

#include <array>
#include <atomic>
#include <cstdint>

#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

template <typename T, uint32_t Size>
class CrossCoreFIFO {
  static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

 public:
  CrossCoreFIFO() : head(0), tail(0) {}

  FORCE_INLINE bool push(const T& item) {
    uint32_t next_head =
        (head.load(std::memory_order_relaxed) + 1) & (Size - 1);

    if (next_head == tail.load(std::memory_order_acquire)) {
      return false;  // Buffer full
    }

    data[head.load(std::memory_order_relaxed)] = item;
    head.store(next_head, std::memory_order_release);
    return true;
  }

  FORCE_INLINE bool pop(T& item) {
    uint32_t current_tail = tail.load(std::memory_order_relaxed);

    if (current_tail == head.load(std::memory_order_acquire)) {
      return false;  // Buffer empty
    }

    item = data[current_tail];
    tail.store((current_tail + 1) & (Size - 1), std::memory_order_release);
    return true;
  }

  FORCE_INLINE bool empty() const {
    return head.load(std::memory_order_acquire) ==
           tail.load(std::memory_order_acquire);
  }

  FORCE_INLINE uint32_t size() const {
    return (head.load(std::memory_order_acquire) -
            tail.load(std::memory_order_acquire)) &
           (Size - 1);
  }

  FORCE_INLINE void clear() {
    head.store(0, std::memory_order_release);
    tail.store(0, std::memory_order_release);
  }

 private:
  std::array<T, Size> data;
  std::atomic<uint32_t> head;
  std::atomic<uint32_t> tail;
};

struct CpuRegisterDump {
  bool     valid;
  bool     is_6309_native;
  uint16_t pc;       // Injected SWI address (instruction stopped at)
  uint16_t s;        // Stack pointer prior to SWI
  uint16_t u;
  uint16_t y;
  uint16_t x;
  uint8_t  dp;
  uint8_t  a;
  uint8_t  b;
  uint8_t  e;        // 6309 register E (0 in 6809 mode)
  uint8_t  f;        // 6309 register F (0 in 6809 mode)
  uint8_t  cc;
  uint8_t  streak_len;
};

#endif  // TURBOLAB_CROSS_CORE_FIFO_H_
