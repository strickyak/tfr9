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
    char* p = Buffer;
    // putbyte(C_EVENT);
    *p++ = (event);
    *p++ = (sz);
    *p++ = (current_opcode_pc >> 8);
    *p++ = (current_opcode_pc);
    *p++ = (current_opcode_cy >> 24);
    *p++ = (current_opcode_cy >> 16);
    *p++ = (current_opcode_cy >> 8);
    *p++ = (current_opcode_cy);
    for (byte i = 0; i < sz; i++) {
      *p++ = (hist_data[i]);
      *p++ = (hist_addr[i] >> 8);
      *p++ = (hist_addr[i]);
    }
    T::TransmitMessage(C_EVENT, p - Buffer, Buffer);
    Noisy();

    if (T::DoesDumpRamOnEvent()) {
      T::DumpRam();
    }
  }

  // unused?
  static void SendEventRam(byte event, byte sz, word base_addr) {
    if (T::DoesLog()) {
      Quiet();
      char* p = Buffer;
      // putbyte(C_EVENT);
      *p++ = (event);
      *p++ = (sz);
      *p++ = (base_addr >> 8);
      *p++ = (base_addr);
      for (byte i = 0; i < sz; i++) {
        *p++ = (T::Peek(base_addr + i));
      }
      T::TransmitMessage(C_EVENT, p - Buffer, Buffer);
      Noisy();
    }
  }
};

#endif  // _EVENT_H_
