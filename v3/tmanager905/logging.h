#ifndef _LOGGING_H_
#define _LOGGING_H_

#define LOG0 T::Logf
#define LOG1 \
  if (1 <= T::LoggingMax() && 1 <= Verbosity) T::Logf
#define LOG2 \
  if (2 <= T::LoggingMax() && 2 <= Verbosity) T::Logf
#define LOG3 \
  if (3 <= T::LoggingMax() && 3 <= Verbosity) T::Logf
#define LOG4 \
  if (4 <= T::LoggingMax() && 4 <= Verbosity) T::Logf
#define LOG5 \
  if (5 <= T::LoggingMax() && 5 <= Verbosity) T::Logf
#define LOG6 \
  if (6 <= T::LoggingMax() && 6 <= Verbosity) T::Logf
#define LOG7 \
  if (7 <= T::LoggingMax() && 7 <= Verbosity) T::Logf
#define LOG8 \
  if (8 <= T::LoggingMax() && 8 <= Verbosity) T::Logf
#define LOG9 \
  if (9 <= T::LoggingMax() && 9 <= Verbosity) T::Logf

enum log_level : uint {
  LFatal = 0,   // Game over
  LHello = 1,   // One-time startup progress
  LUpDown = 2,  // Device drivers up & down
  L3 = 3,
  LIO = 4,       // IO of disk blocks or network packets
  LDefault = 5,  // Default level, if you don't care
  LCall = 6,     // Operating system calls and returns
  LIrq = 7,      // Interrupts
  LDebug = 8,    // Verbose debugging
  LDetail = 9,   // Even verboser debugging
};

uint Verbosity = 9;
char Buffer[300];

template <typename T, uint MAX>
struct Logging {
  constexpr static uint LoggingMax() { return MAX; }
  constexpr static bool DoesLog() { return MAX >= LDefault; }

  static void Logf(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vLogf(C_LOGGING + LDefault, LDefault, fmt, va);
    va_end(va);
  }

  static void Logf(enum log_level level, const char* fmt, ...) {
    // Compile time limits.
    if (level > MAX) return;

    // Runtime limits.
    if (quiet_ram > 0) return;
    if (level > Verbosity) return;

    va_list va;
    va_start(va, fmt);
    vLogf(C_LOGGING + level, level, fmt, va);
    va_end(va);
  }

  static void vLogf(byte messtype, enum log_level level, const char* fmt,
                    va_list va) {
    // Compile time limits.
    if (level > MAX) return;

    // Runtime limits.
    if (quiet_ram > 0) return;
    if (level > Verbosity) return;

    // TODO: Locking on Buffer.
    vsprintf(Buffer, fmt, va);

    uint sz = strlen(Buffer);
    if (sz > 1) {
      if (Buffer[sz - 1] == '\n') {
        Buffer[sz - 1] = '\0';
        sz--;
      }
    }

    TransmitMessage(messtype, sz, Buffer);
  }

  static void TransmitMessage(byte messtype, uint sz, char* buf) {
    if (messtype < 0xC0) {
      TransmitHeader(messtype, sz);
    } else {
      putbyte(messtype);
      assert((messtype & 15) == sz);
    }
    for (uint i = 0; i < sz; i++) {
      putbyte(buf[i]);
    }
  }

  static void TransmitHeader(byte messtype, uint sz) {
    putbyte(messtype);
    if (sz < 64) {
      putbyte(128 + sz);
    } else {
      assert(sz <= 1024);             // really could go up to 4095
      putbyte(128 + 64 + (sz >> 6));  // send sz mod 64
      putbyte(128 + (sz & 63));       // send sz div 64
    }
  }
};

#endif  // _LOGGING_H_
