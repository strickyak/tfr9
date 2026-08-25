#ifndef V4_CORE_ENGINE_H_
#define V4_CORE_ENGINE_H_

// v4_core_engine.h — The shared background engine and inter-core glue.
//
// CoreEngine<T> provides:
//   - The CrossCoreFIFO-based fg2bg/bg2fg communication.
//   - FORCE_INLINE helpers for the foreground loop (PushFifoRead/Write).
//   - COBS packet encoding.
//   - Default background event handlers.
//   - RunCores() to launch foreground on core1, background on core0.
//
// The actual foreground and background loops are FREE FUNCTIONS
// (not class methods) so they can be marked IN_RAM:
//
//   template<typename T> void IN_RAM tfr911_foreground_loop();
//   template<typename T> void IN_RAM centipede_foreground_loop();
//   template<typename T> void IN_RAM v4_background_loop();
//
// GCC does not support __attribute__((section)) on class methods.
// All T::methods called from these loops are FORCE_INLINE static,
// so they compile into the IN_RAM function's body with zero call
// overhead and no FLASH fetch stalls.
//
// Platform engines (TFR911Engine, CentipedeEngine) provide:
//   - InitializePins() — GPIO setup (IN_FLASH, called once).
//   - InitPIO() — PIO program loading (IN_FLASH, called once).
//   - Assert/Release IRQ/FIRQ/NMI pins (FORCE_INLINE).
//   - HaltOn/HaltOff (FORCE_INLINE).
//   - ReadRam/WriteRam/ReadIO/WriteIO (FORCE_INLINE).

#include <array>
#include <atomic>
#include <string>

#include "v4_types.h"
#include "v4_ram.h"

// ── Cross-Core FIFO ──
// Lock-free SPSC ring buffer for inter-core communication.
// Size must be a power of 2.
template <typename ElemT, uint32_t Size>
class CrossCoreFIFO {
  static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

 public:
  CrossCoreFIFO() : head_(0), tail_(0) {}

  FORCE_INLINE bool push(const ElemT& item) {
    uint32_t next = (head_.load(std::memory_order_relaxed) + 1) & (Size - 1);
    if (next == tail_.load(std::memory_order_acquire)) return false;
    data_[head_.load(std::memory_order_relaxed)] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }

  FORCE_INLINE bool pop(ElemT& item) {
    uint32_t cur = tail_.load(std::memory_order_relaxed);
    if (cur == head_.load(std::memory_order_acquire)) return false;
    item = data_[cur];
    tail_.store((cur + 1) & (Size - 1), std::memory_order_release);
    return true;
  }

  FORCE_INLINE uint32_t size() const {
    return (head_.load(std::memory_order_acquire) -
            tail_.load(std::memory_order_acquire)) &
           (Size - 1);
  }

 private:
  std::array<ElemT, Size> data_;
  std::atomic<uint32_t> head_;
  std::atomic<uint32_t> tail_;
};

// ── FIFO instances ──
// Foreground → Background (cycle logs, putchar, NMI, floppy commands)
inline CrossCoreFIFO<uint, 8192> fg2bg;
// Background → Foreground (peek/poke commands from Tcl console)
inline CrossCoreFIFO<uint, 8192> bg2fg;

// ── Flow control ──
inline constexpr uint FG2BG_HIGH_WATERMARK = 6000;
inline constexpr uint FG2BG_LOW_WATERMARK  = 2000;
inline volatile bool fg_halt_for_flow_control = false;

// ── Macro for pushing to fg2bg ──
#define PUSH_TO_BG(TAG, ADDR, DATA) \
  fg2bg.push((static_cast<uint>(TAG) << 24) | ((ADDR) << 8) | (DATA))

#define SAY(CH) PUSH_TO_BG(FG2BG_PUTCHAR, 0, (CH) & 255)

// ── CoreEngine<T> ──
// The shared engine base class. T is the final Engine class (CRTP).
// Provides FORCE_INLINE helpers and non-hot-path utility functions.
// The actual background loop is the free function v4_background_loop<T>().
template <class T>
class CoreEngine {
 public:

  // ── ShowChar / ShowString ──
  // Send a character or string to the tether via the fg2bg FIFO.
  // These are callable from both foreground and background contexts.
  FORCE_INLINE static void ShowChar(byte ch) {
    SAY(ch);
  }

  static void ShowString(const char* s) {
    while (*s) {
      SAY(*s);
      s++;
    }
  }

  // ── CobsTransmit ──
  // Encode and send a COBS packet over USB.
  // This wraps the shared cobs.h CobsEncodeAndTransmit().
  static void CobsTransmit(const unsigned char* data, size_t len) {
    // Delegates to the platform's putchar_raw or equivalent.
    CobsEncodeAndTransmit(data, len, T::PutCharRaw);
  }

  // ── PushFifoRead / PushFifoWrite ──
  // Called by the foreground inner loop to log bus cycles.
  // FORCE_INLINE so they inline into the IN_RAM foreground function.
  FORCE_INLINE static void PushFifoRead(uint addr, byte data) {
    PUSH_TO_BG(FG2BG_READ, addr, data);
  }

  FORCE_INLINE static void PushFifoWrite(uint addr, byte data) {
    PUSH_TO_BG(FG2BG_WRITE, addr, data);
  }

  // ── RunCores ──
  // Launch foreground on core1, background on core0.
  // core1_fn and core0_fn are IN_RAM free function trampolines.
  static void RunCores(void (*core1_fn)(), void (*core0_fn)()) {
    multicore_launch_core1(core1_fn);
    core0_fn();  // Background runs on core0 (never returns).
  }

  // ── Default background event handlers ──
  // Override in the platform engine or CRTP mixin as needed.
  static void EmitPutChar(byte ch) {
    unsigned char pkt[] = {C_PUTCHAR, ch};
    CobsTransmit(pkt, sizeof(pkt));
  }

  static void OnBackgroundWrite(uint addr, byte data) {
    // Default: log to tether if tracing is on.
    T::OnWriteTrace(addr, data);
  }

  static void OnBackgroundRead(uint addr, byte data) {
    // Default: no-op. Override for cycle logging.
  }

  static void OnBackgroundChore(uint tag, uint addr, byte data) {
    // Default: ignore unknown chores.
  }
};

// ══════════════════════════════════════════════════════════════════
// v4_background_loop<T>() — The shared background task loop.
//
// Runs on core0. Polls USB, drains fg2bg FIFO, dispatches packets.
// In the full implementation this will use coroutines for
// drain_task, floppy_task, and spoon_task (Tcl console).
//
// This is a free function so it CAN be marked IN_RAM if needed,
// although the background is less performance-critical than the
// foreground.
//
// Called from a trampoline:
//   void IN_RAM core0_trampoline() {
//       v4_background_loop<Engine>();
//   }
// ══════════════════════════════════════════════════════════════════
template <typename T>
void IN_RAM v4_background_loop() {
  T::ShowString("Background: starting.\n");

  while (true) {
    // 1. Poll USB for incoming bytes and decode COBS packets.
    T::PollUsbInput();

    // 2. Drain fg2bg FIFO — handle putchar, read/write logs, etc.
    uint chore;
    while (fg2bg.pop(chore)) {
      uint tag  = chore >> 24;
      uint addr = (chore >> 8) & 0xFFFF;
      byte data = chore & 0xFF;

      switch (tag) {
        case FG2BG_PUTCHAR:
          T::EmitPutChar(data);
          break;
        case FG2BG_WRITE:
          T::OnBackgroundWrite(addr, data);
          break;
        case FG2BG_READ:
          T::OnBackgroundRead(addr, data);
          break;
        default:
          T::OnBackgroundChore(tag, addr, data);
          break;
      }
    }

    // 3. Check bg2fg for console peek/poke (handled by foreground).
    // (Foreground polls bg2fg directly in its loop.)
  }
}

#endif  // V4_CORE_ENGINE_H_
