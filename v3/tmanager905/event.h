#ifndef _EVENT_H_
#define _EVENT_H_

template <typename T>
struct DontDumpRamOnEvent {
  static constexpr bool DoesDumpRamOnEvent() { return false; }
};

template <typename T>
struct DoDumpRamOnEvent {
  static constexpr bool DoesDumpRamOnEvent() { return true; }
};

template <typename T>
struct DontEvent {
  static constexpr bool DoesEvent() { return false; }
  static force_inline void SendEventHist(byte event, byte sz) {}
  static force_inline void SendEventRam(byte event, byte sz, word base_addr) {}
};

template <typename T>
struct DoEvent {
  static constexpr bool DoesEvent() { return true; }

  static void SendEventHist(byte event, byte sz) {
    Quiet();
    putbyte(C_EVENT);
    putbyte(event);
    putbyte(sz);
    putbyte(current_opcode_pc >> 8);
    putbyte(current_opcode_pc);
    putbyte(current_opcode_cy >> 24);
    putbyte(current_opcode_cy >> 16);
    putbyte(current_opcode_cy >> 8);
    putbyte(current_opcode_cy);
    for (byte i = 0; i < sz; i++) {
      putbyte(hist_data[i]);
      putbyte(hist_addr[i] >> 8);
      putbyte(hist_addr[i]);
    }
    Noisy();

    if (T::DoesDumpRamOnEvent()) {
      T::DumpRam();
    }
  }

  // unused?
  static void SendEventRam(byte event, byte sz, word base_addr) {
    if (T::DoesLog()) {
      Quiet();
      putbyte(C_EVENT);
      putbyte(event);
      putbyte(sz);
      putbyte(base_addr >> 8);
      putbyte(base_addr);
      for (byte i = 0; i < sz; i++) {
        putbyte(T::Peek(base_addr + i));
      }
      Noisy();
    }
  }
};

#endif  // _EVENT_H_
