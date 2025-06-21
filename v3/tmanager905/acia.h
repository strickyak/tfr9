#ifndef _ACIA_H_
#define _ACIA_H_

// Motorola 6850 Asynchronous Commuication Interface Adapter (UART)


template <typename T>
struct DontAcia {
  constexpr static bool DoesAcia() { return false; }
};
template <typename T>
struct DoAcia {
  constexpr static bool DoesAcia() { return true; }
};


#endif // _ACIA_H_
