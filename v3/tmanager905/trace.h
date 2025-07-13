#ifndef _TRACE_H_
#define _TRACE_H_

uint Traceosity = 5;

enum cycle_kind : byte {
  CY_UNUSED = 0,
  CY_SEEN = 1,
  CY_UNSEEN = 2,
  CY_MORE = 3,
  CY_READ = 4,
  CY_WRITE = 5,
  CY_IDLE = 6,
};

template <typename T>
struct DontTrace {
  static constexpr bool DoesTrace() { return false; }
  static int vTracef(const char* fmt, va_list va) { return 0; }
  static force_inline int Tracef(const char* fmt, ...) { return 0; }

  static void TransmitCycle(uint cy, byte flags, byte kind, byte data,
                            uint addr) {}
};

template <typename T>
struct DoTrace {
  static force_inline bool DoesTrace() { return enable_trace; }

  static int vTracef(const char* fmt, va_list va) {
    if (!interest) return 0;
    if (quiet_ram > 0) return 0;

    int z = vprintf(fmt, va);
    return z;
  }
  static int Tracef(const char* fmt, ...) {
    if (!interest) return 0;
    if (quiet_ram > 0) return 0;

    va_list va;
    va_start(va, fmt);
    int z = vprintf(fmt, va);
    va_end(va);
    return z;
  }

  static void TransmitCycle(uint cy, byte flags, byte kind, byte data,
                            uint addr) {
    if (Traceosity < 6) return;
    if ((flags & 0xE0) != (CY_UNSEEN << 5) && Traceosity < 7) return;
    if ((flags & 0xE0) != (CY_SEEN << 5) && Traceosity < 8) return;

    byte r[8];
    r[0] = cy >> 24;
    r[1] = cy >> 16;
    r[2] = cy >> 8;
    r[3] = cy >> 0;
    r[4] = flags + (kind << 5);
    r[5] = data;
    r[6] = addr >> 8;
    r[7] = addr >> 0;
    T::TransmitMessage(C_CYCLE, 8, (char*)r);
  }
};

#endif  // _TRACE_H_
