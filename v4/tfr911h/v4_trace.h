#ifndef V4_TRACE_H_
#define V4_TRACE_H_

// v4_trace.h — CRTP mixins for CPU cycle tracing.
//
// DoTrace<T> sends cycle information to the tether for debugging.
//   - TFR911 uses C_CYCLE (uncompressed 8-byte format with FIC).
//   - Centipede uses C_COMPRESSED_CYCLES (batched, compressed format).
//   - Both can also use C_RAM2_WRITE for write-only tracing.
//
// DontTrace<T> compiles to nothing (zero overhead when tracing is off).
//
// The foreground inner loop calls these inline hooks:
//   T::OnReadTrace(addr, data)
//   T::OnWriteTrace(addr, data)
//   T::OnFICTrace(cycle, flags, kind, data, addr)

#include "v4_types.h"

// ── DontTrace<T> ──
template <typename T>
struct DontTrace {
  FORCE_INLINE static void OnReadTrace(uint addr, byte data) {}
  FORCE_INLINE static void OnWriteTrace(uint addr, byte data) {}
  FORCE_INLINE static void OnFICTrace(uint cycle, byte flags, byte kind,
                                      byte data, uint addr) {}
  FORCE_INLINE static void TransmitWrite(uint addr, byte data) {}
  FORCE_INLINE static void TransmitCycle(uint cy, byte flags, byte kind,
                                         byte data, uint addr) {}
};

// ── DoTrace<T> ──
// Sends C_CYCLE packets (TFR911 uncompressed format).
// The Centipede can override with compressed cycles in its own mixin.
template <typename T>
struct DoTrace {
  // Called on write cycles for write-only tracing.
  FORCE_INLINE static void TransmitWrite(uint addr, byte data) {
    unsigned char pkt[] = {C_RAM2_WRITE,
                           static_cast<byte>(addr >> 8),
                           static_cast<byte>(addr),
                           data};
    T::CobsTransmit(pkt, sizeof(pkt));
  }

  // Called to transmit a full cycle record (TFR911 8-byte format).
  FORCE_INLINE static void TransmitCycle(uint cy, byte flags, byte kind,
                                         byte data, uint addr) {
    byte kind_fl = (kind << 5) | (flags & 31);
    unsigned char pkt[] = {
        C_CYCLE,
        static_cast<byte>(cy >> 24),
        static_cast<byte>(cy >> 16),
        static_cast<byte>(cy >> 8),
        static_cast<byte>(cy),
        kind_fl,
        data,
        static_cast<byte>(addr >> 8),
        static_cast<byte>(addr)};
    T::CobsTransmit(pkt, sizeof(pkt));
  }

  FORCE_INLINE static void OnReadTrace(uint addr, byte data) {
    // Read tracing is handled via TransmitCycle in the inner loop.
  }

  FORCE_INLINE static void OnWriteTrace(uint addr, byte data) {
    TransmitWrite(addr, data);
  }

  FORCE_INLINE static void OnFICTrace(uint cycle, byte flags, byte kind,
                                      byte data, uint addr) {
    TransmitCycle(cycle, flags, kind, data, addr);
  }
};

#endif  // V4_TRACE_H_
