#ifndef _LOG_H_
#define _LOG_H_


template <class T>
struct DontLog {
  static constexpr bool DoesLog() { return false; }
  static constexpr bool DoesTrace() { return false; }
  static force_inline int Logf(const char* fmt, ...) { return 0; }
};
template <class T>
struct DoLog {
  static constexpr bool DoesLog() { return true; }
  static force_inline bool DoesTrace() { return enable_trace; }

  static int vLogf(const char* fmt, va_list va) {
    if (!interest) return 0;
    if (quiet_ram > 0) return 0;

    int z = vprintf(fmt, va);
    return z;
  }
  static int Logf(const char* fmt, ...) {
    if (!interest) return 0;
    if (quiet_ram > 0) return 0;

    va_list va;
    va_start(va, fmt);
    int z = vprintf(fmt, va);
    va_end(va);
    return z;
  }
};



#endif // _LOG_H_
