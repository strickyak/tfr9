#pragma once
// becker.h -- DriveWire / FujiNet Becker port over UART0 (default GPIO 12 TX, GPIO 13 RX)
// plus interactive FujiNet hat hardware & protocol diagnostics (`fujinet` Tcl command).
//
// Default address in enhancement map `e1`: 0xFF41 (status), 0xFF42 (data);
// on `jamesbrown`: 0xFF45..0xFF46.
// Status read: bit 1 (0x02) set when RX data is waiting, else 0x00.
// Data read:   next RX byte (0xFF if empty); Data write: enqueue TX byte to UART.
//
// Core 1 touches only lock-free SRAM rings (`rx_ring`, `tx_ring`).  On core 0, `UartIrq` fills
// `rx_ring` and `Pump()` drains `tx_ring` onto UART0.  Honors `dw.baud`, `dw.polarity` (or
// `dw.over = bitbang` for inverted CoCo bitbanger UART), `dw.tx`, and `dw.rx` from `g_port_settings`.

#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <hardware/uart.h>

namespace becker {

#define BECKER_UART uart0
constexpr uint kDefaultBaud = 921600;
constexpr uint kTxPin = 12;
constexpr uint kRxPin = 13;

inline CrossCoreFIFO<byte, 2048> rx_ring;
inline CrossCoreFIFO<byte, 512> tx_ring;

inline volatile uint32_t stat_rx_bytes = 0;
inline volatile uint32_t stat_tx_bytes = 0;
inline volatile uint32_t stat_rx_drops = 0;
inline volatile uint32_t stat_tx_drops = 0;
inline volatile uint32_t stat_overruns = 0;
inline volatile uint32_t stat_empty_reads = 0;
inline uint32_t active_baud = kDefaultBaud;
inline uint active_tx_pin = kTxPin;
inline uint active_rx_pin = kRxPin;
inline bool active_tx_invert = false;
inline bool active_rx_invert = false;
inline uint16_t port_base = 0xFF41;
inline bool hw_inited = false;
inline bool active = false;

inline bool IsValidUart0Tx(uint p) { return p == 12 || p == 14 || p == 16 || p == 18; }
inline bool IsValidUart0Rx(uint p) { return p == 13 || p == 15 || p == 17 || p == 19; }

inline const char* PolarityName(bool tx_inv, bool rx_inv) {
  if (!tx_inv && !rx_inv) return "normal";
  if (tx_inv && rx_inv)   return "invert";
  if (tx_inv && !rx_inv)  return "tx_invert(dragon)";
  return "rx_invert";
}

inline bool ParsePolarity(const char* s, bool* tx_inv, bool* rx_inv) {
  if (!strcmp(s, "normal") || !strcmp(s, "0")) { *tx_inv = false; *rx_inv = false; return true; }
  if (!strcmp(s, "invert") || !strcmp(s, "inverted") || !strcmp(s, "1")) { *tx_inv = true; *rx_inv = true; return true; }
  if (!strcmp(s, "dragon") || !strcmp(s, "tx_invert")) { *tx_inv = true; *rx_inv = false; return true; }
  if (!strcmp(s, "rx_invert")) { *tx_inv = false; *rx_inv = true; return true; }
  return false;
}

static void IN_RAM UartIrq() {
  uart_hw_t* hw = uart_get_hw(BECKER_UART);
  if (hw->rsr & UART_UARTRSR_OE_BITS) {
    stat_overruns++;
    hw->rsr = UART_UARTRSR_BITS;
  }
  while (uart_is_readable(BECKER_UART)) {
    byte b = (byte)uart_getc(BECKER_UART);
    if (rx_ring.push(b)) stat_rx_bytes++;
    else stat_rx_drops++;
  }
}

inline void Flush() {
  byte b;
  while (tx_ring.pop(b)) {}
  while (rx_ring.pop(b)) {}
}

inline void Deinit() {
  if (!hw_inited) return;
  irq_set_enabled(UART_IRQ_NUM(BECKER_UART), false);
  uart_set_irqs_enabled(BECKER_UART, false, false);
  uart_deinit(BECKER_UART);
  gpio_set_outover(active_tx_pin, GPIO_OVERRIDE_NORMAL);
  gpio_set_inover(active_rx_pin, GPIO_OVERRIDE_NORMAL);
  gpio_init(active_tx_pin);
  gpio_set_dir(active_tx_pin, GPIO_IN);
  gpio_pull_up(active_tx_pin);
  gpio_init(active_rx_pin);
  gpio_set_dir(active_rx_pin, GPIO_IN);
  gpio_pull_up(active_rx_pin);
  hw_inited = false;
  Flush();
}

inline void EnsureInit(uint32_t baud = active_baud, uint tx_pin = active_tx_pin,
                       uint rx_pin = active_rx_pin, bool tx_invert = active_tx_invert,
                       bool rx_invert = active_rx_invert) {
  if (!IsValidUart0Tx(tx_pin)) tx_pin = kTxPin;
  if (!IsValidUart0Rx(rx_pin)) rx_pin = kRxPin;
  if (!hw_inited || tx_pin != active_tx_pin || rx_pin != active_rx_pin ||
      tx_invert != active_tx_invert || rx_invert != active_rx_invert) {
    if (hw_inited) Deinit();
    if (rx_invert) gpio_pull_down(rx_pin);
    else gpio_pull_up(rx_pin);
    gpio_set_function(tx_pin, UART_FUNCSEL_NUM(BECKER_UART, tx_pin));
    gpio_set_function(rx_pin, UART_FUNCSEL_NUM(BECKER_UART, rx_pin));
    // Must apply pad overrides AFTER gpio_set_function (which zeroes IO_BANK0 ctrl).
    gpio_set_outover(tx_pin, tx_invert ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    gpio_set_inover(rx_pin, rx_invert ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    uart_init(BECKER_UART, baud);
    uart_set_format(BECKER_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(BECKER_UART, false, false);
    uart_set_fifo_enabled(BECKER_UART, true);
    busy_wait_us_32(150);
    while (uart_is_readable(BECKER_UART)) (void)uart_getc(BECKER_UART);
    uart_get_hw(BECKER_UART)->rsr = UART_UARTRSR_BITS;
    Flush();
    irq_set_exclusive_handler(UART_IRQ_NUM(BECKER_UART), UartIrq);
    irq_set_enabled(UART_IRQ_NUM(BECKER_UART), true);
    uart_set_irqs_enabled(BECKER_UART, true, false);
    hw_inited = true;
    active_baud = baud;
    active_tx_pin = tx_pin;
    active_rx_pin = rx_pin;
    active_tx_invert = tx_invert;
    active_rx_invert = rx_invert;
  } else if (baud != active_baud) {
    uart_set_baudrate(BECKER_UART, baud);
    active_baud = baud;
    busy_wait_us_32(80);
    while (uart_is_readable(BECKER_UART)) (void)uart_getc(BECKER_UART);
    uart_get_hw(BECKER_UART)->rsr = UART_UARTRSR_BITS;
    Flush();
  }
}

inline void Pump() {
  if (!hw_inited) return;
  byte b;
  while (uart_is_writable(BECKER_UART) && tx_ring.pop(b)) {
    uart_putc_raw(BECKER_UART, b);
    stat_tx_bytes++;
  }
}

inline uint Transact(const byte* out, uint n, byte* in, uint want, uint ms) {
  EnsureInit(active_baud, active_tx_pin, active_rx_pin, active_tx_invert, active_rx_invert);
  Flush();
  for (uint k = 0; k < n; k++) tx_ring.push(out[k]);
  uint got = 0;
  uint64_t deadline = time_us_64() + (uint64_t)ms * 1000ULL;
  while ((got < want || tx_ring.size()) && time_us_64() < deadline) {
    Pump();
    byte b;
    if (got < want && rx_ring.pop(b)) in[got++] = b;
  }
  return got;
}

inline bool ValidOpTime(const byte t[6]) {
  // t[0]=year-1900 (69..255; unsynced epoch in UTC-x is 1969-12-31 -> 69),
  // t[1]=month (1..12), t[2]=day (1..31), t[3]=hour (0..23), t[4]=min (0..59), t[5]=sec (0..60)
  return t[0] >= 69 && t[1] >= 1 && t[1] <= 12 && t[2] >= 1 && t[2] <= 31 &&
         t[3] <= 23 && t[4] <= 59 && t[5] <= 60;
}

// Diagnostic software bit-banged 8N1 UART transceiver for arbitrary GPIO pairs at TCL>
// (e.g. swapped pins, IO26 on GPIO 14, or unbodged 40c on GPIO 8/9).
inline uint DiagBitbangTransact(uint tx_pin, uint rx_pin, uint32_t baud, bool tx_invert, bool rx_invert,
                                const byte* out, uint n, byte* in, uint want, uint ms) {
  const bool tx_idle_lvl = !tx_invert;
  const bool rx_start_lvl = rx_invert;
  gpio_init(tx_pin);
  gpio_put(tx_pin, tx_idle_lvl);
  gpio_set_dir(tx_pin, GPIO_OUT);
  gpio_init(rx_pin);
  gpio_set_dir(rx_pin, GPIO_IN);
  if (rx_invert) gpio_pull_down(rx_pin);
  else gpio_pull_up(rx_pin);
  busy_wait_us_32(150);

  for (uint i = 0; i < n; i++) {
    uint16_t frame = (uint16_t)(((uint16_t)out[i] << 1) | (1u << 9));  // start=0, 8 data, stop=1
    uint32_t irq_state = save_and_disable_interrupts();
    uint64_t t0 = time_us_64();
    for (uint bit = 0; bit < 10; bit++) {
      bool b = (frame >> bit) & 1u;
      gpio_put(tx_pin, tx_invert ? !b : b);
      uint64_t next_t = t0 + ((uint64_t)(bit + 1) * 1000000ULL + baud / 2) / baud;
      while (time_us_64() < next_t) {}
    }
    restore_interrupts(irq_state);
  }

  uint got = 0;
  uint64_t deadline = time_us_64() + (uint64_t)ms * 1000ULL;
  while (got < want && time_us_64() < deadline) {
    if (gpio_get(rx_pin) == rx_start_lvl) {
      uint32_t irq_state = save_and_disable_interrupts();
      uint64_t t0 = time_us_64();
      // Sample mid-start-bit to reject narrow glitches
      uint64_t half_bit = t0 + (500000ULL + baud / 2) / baud;
      while (time_us_64() < half_bit) {}
      if (gpio_get(rx_pin) != rx_start_lvl) {
        restore_interrupts(irq_state);
        continue;
      }
      byte val = 0;
      for (uint bit = 0; bit < 8; bit++) {
        uint64_t sample_t = t0 + ((uint64_t)(2 * bit + 3) * 500000ULL + baud / 2) / baud;
        while (time_us_64() < sample_t) {}
        bool lvl = gpio_get(rx_pin);
        bool bit_val = rx_invert ? !lvl : lvl;
        if (bit_val) val |= (byte)(1u << bit);
      }
      // Wait into stop bit
      uint64_t stop_t = t0 + (19ULL * 500000ULL + baud / 2) / baud;
      while (time_us_64() < stop_t) {}
      restore_interrupts(irq_state);
      in[got++] = val;
    }
  }

  gpio_init(tx_pin);
  gpio_set_dir(tx_pin, GPIO_IN);
  gpio_pull_up(tx_pin);
  gpio_init(rx_pin);
  gpio_set_dir(rx_pin, GPIO_IN);
  gpio_pull_up(rx_pin);
  return got;
}

constexpr byte kOpTime = 0x23;
constexpr byte kOpDwInit = 0x5A;
constexpr byte kOpFuji = 0xE2;
constexpr byte kFujiGetResponse = 0x01, kFujiGetError = 0x02;
constexpr byte kFujiGetAdapterConfig = 0xE8;
constexpr byte kFujiReadDeviceSlots = 0xF2, kFujiReadHostSlots = 0xF4;
constexpr byte kFujiMountImage = 0xF8, kFujiMountHost = 0xF9, kFujiGetWifiStatus = 0xFA;
constexpr byte kFujiOk = 0x01;
constexpr uint kDeviceSlotSize = 38;
constexpr uint kDeviceSlots = 4;
constexpr uint kHostSlotSize = 32;
constexpr uint kHostSlots = 8;
constexpr uint kAdapterConfigSize = 140;

inline bool FujiCmd(const byte* req, uint n, uint timeout_ms = 15000) {
  byte status = 0;
  Transact(req, n, nullptr, 0, 50);
  const byte get_error[2] = {kOpFuji, kFujiGetError};
  return Transact(get_error, 2, &status, 1, timeout_ms) == 1 && status == kFujiOk;
}

inline const char* MountSlot(uint slot, char* name, uint name_size) {
  const byte read_slots[2] = {kOpFuji, kFujiReadDeviceSlots};
  if (!FujiCmd(read_slots, 2)) return "no answer from the FujiNet";
  byte slots[kDeviceSlots * kDeviceSlotSize];
  const byte get_response[2] = {kOpFuji, kFujiGetResponse};
  if (Transact(get_response, 2, slots, sizeof slots, 2000) < sizeof slots)
    return "the FujiNet did not send its device slots";
  const byte* d = slots + slot * kDeviceSlotSize;
  snprintf(name, name_size, "%.36s", (const char*)d + 2);
  if (d[0] >= 8 || !name[0]) return "nothing is in that device slot";
  const byte mount_host[3] = {kOpFuji, kFujiMountHost, d[0]};
  if (!FujiCmd(mount_host, 3)) return "the FujiNet could not mount the slot's host";
  const byte mount_image[4] = {kOpFuji, kFujiMountImage, (byte)slot, d[1] == 2 ? (byte)2 : (byte)1};
  if (!FujiCmd(mount_image, 4)) return "the FujiNet could not mount the image";
  return nullptr;
}

enum PinState : uint8_t { PIN_FLOAT = 0, PIN_LOW = 1, PIN_HIGH = 2, PIN_TOGGLE = 3 };

struct HatPinDesc {
  uint gpio;
  const char* j1;
  const char* role;
};

inline constexpr HatPinDesc kHatPins[] = {
#if !defined(_BOARDS_CENTIPEDE32Z_H)
  { 8, "J1-1  H8   ", "40c-unbodged: R2(1K)<-ESP32 IO21; 40c-bodged/41d: NC"},
  { 9, "J1-3  H9   ", "40c-unbodged: R1(1K)->ESP32 IO33; 40c-bodged/41d: NC"},
#endif
  {10, "J1-5  H10CD", "40c & 41d: R5(1K)<-ESP32 IO22 (PIN_CD, pull-up)"},
#if !defined(_BOARDS_CENTIPEDE32Z_H)
  {11, "J1-7  H11  ", "NC"},
#endif
  {12, "J1-9  H12  ", "40c-bodged & 41d: J2-8 ->ESP32 IO33 (UART RX / CASS_IN)"},
  {13, "J1-11 H13  ", "40c-bodged: J3-15<-ESP32 IO21 (UART TX); 41d: J2-15=IO13!"},
  {14, "J1-13 H14  ", "40c-bodged & 41d: J2-10<-ESP32 IO26 (CASS_OUT)"},
  {15, "J1-15 H15  ", "40c-bodged & 41d: J2-9 <-ESP32 IO25 (DAC1)"},
  {16, "J1-17 H16  ", "J4-1 expansion"},
  {17, "J1-19 H17  ", "J4-3 expansion"},
  {18, "J1-20 H18  ", "J4-5 expansion"},
  {19, "J1-18 H19  ", "J4-7 expansion"},
};

inline PinState ProbeSinglePin(uint gpio, uint32_t* out_edges = nullptr) {
  gpio_init(gpio);
  gpio_set_dir(gpio, GPIO_IN);
  gpio_pull_down(gpio);
  busy_wait_us_32(40);
  bool pd = gpio_get(gpio);

  gpio_pull_up(gpio);
  busy_wait_us_32(40);
  bool pu = gpio_get(gpio);

  uint32_t edges = 0;
  bool prev = pu;
  uint64_t end_t = time_us_64() + 3000ULL;
  while (time_us_64() < end_t) {
    bool cur = gpio_get(gpio);
    if (cur != prev) { edges++; prev = cur; }
  }
  if (out_edges) *out_edges = edges;
  if (edges > 1) return PIN_TOGGLE;
  if (pd && pu) return PIN_HIGH;
  if (!pd && !pu) return PIN_LOW;
  return PIN_FLOAT;
}

inline const char* PinStateName(PinState s) {
  switch (s) {
    case PIN_HIGH:   return "HIGH  ";
    case PIN_LOW:    return "LOW   ";
    case PIN_TOGGLE: return "TOGGLE";
    default:         return "FLOAT ";
  }
}

inline void AppendPinsReport(Tcl_Interp* interp) {
  bool was_inited = hw_inited;
  uint32_t saved_baud = active_baud;
  uint saved_tx = active_tx_pin, saved_rx = active_rx_pin;
  bool saved_tx_inv = active_tx_invert, saved_rx_inv = active_rx_invert;
  if (was_inited) Deinit();

  PinState st_by_gpio[24];
  for (uint i = 0; i < 24; i++) st_by_gpio[i] = PIN_FLOAT;

  char line[192];
  Tcl_AppendResult(interp, "=== FujiNet Hat Pin Probe ===\n", (char*)NULL);
  for (size_t i = 0; i < sizeof(kHatPins) / sizeof(kHatPins[0]); i++) {
    uint32_t edges = 0;
    PinState st = ProbeSinglePin(kHatPins[i].gpio, &edges);
    if (kHatPins[i].gpio < 24) st_by_gpio[kHatPins[i].gpio] = st;
    if (edges > 1) {
      snprintf(line, sizeof line, "  GPIO %2u (%s): %s (%lu edges/3ms)  [%s]\n",
               kHatPins[i].gpio, kHatPins[i].j1, PinStateName(st),
               (unsigned long)edges, kHatPins[i].role);
    } else {
      snprintf(line, sizeof line, "  GPIO %2u (%s): %s  [%s]\n",
               kHatPins[i].gpio, kHatPins[i].j1, PinStateName(st), kHatPins[i].role);
    }
    Tcl_AppendResult(interp, line, (char*)NULL);
  }

  // Circuit signature interpretation:
  PinState s10 = st_by_gpio[10], s12 = st_by_gpio[12], s13 = st_by_gpio[13];
  PinState s8 = st_by_gpio[8], s9 = st_by_gpio[9];
  Tcl_AppendResult(interp, "Circuit diagnosis: ", (char*)NULL);
  if (s10 == PIN_FLOAT && s12 == PIN_FLOAT && s13 == PIN_FLOAT && s8 == PIN_FLOAT && s9 == PIN_FLOAT) {
    Tcl_AppendResult(interp, "ALL hat pins FLOAT -> hat unpowered, not seated, or ESP32 held in reset.\n", (char*)NULL);
  } else if (s13 == PIN_HIGH && s12 == PIN_HIGH) {
    Tcl_AppendResult(interp,
        "GPIO 12 & GPIO 13 both HIGH -> 40c+bodges / 41d+bodge (TX12->IO33 pull-up & RX13<-IO21 TX idle HIGH) BOTH connected!\n",
        (char*)NULL);
  } else if (s13 == PIN_HIGH && s12 == PIN_FLOAT) {
    Tcl_AppendResult(interp,
        "GPIO 13 driven HIGH (J1-11->J3-15=IO21 OK), but GPIO 12 FLOATS -> check J1-9->J2-8 (IO33) bodge/solder joint or ESP32 RX pinmap!\n",
        (char*)NULL);
  } else if (s13 == PIN_HIGH) {
    Tcl_AppendResult(interp,
        "GPIO 13 driven HIGH -> 40c+bodges (J1-11->J3-15=IO21) with ESP32 TX idle HIGH.\n",
        (char*)NULL);
  } else if (s13 == PIN_LOW) {
    Tcl_AppendResult(interp,
        "GPIO 13 driven LOW -> 40c+bodges (J1-11->J3-15=IO21) with INVERTED UART TX idle (stock CoCo DriveWire UART2) or IO21 held low!\n",
        (char*)NULL);
  } else if (s10 != PIN_FLOAT && s13 == PIN_FLOAT) {
    Tcl_AppendResult(interp,
        "GPIO 10 connected (ESP32 powered), but GPIO 13 FLOATS -> either ESP32 firmware did not enable IO21 TX, or 41d PCB bug (J1-11->J2-15=IO13)!\n",
        (char*)NULL);
  } else if (s8 != PIN_FLOAT || s9 != PIN_FLOAT) {
    Tcl_AppendResult(interp,
        "GPIO 8/9 active while GPIO 13 floats -> looks like UNBODGED 40c (R1/R2 still installed)!\n",
        (char*)NULL);
  } else {
    Tcl_AppendResult(interp, "partial pin activity; run 'fujinet scan' to probe UART combinations.\n", (char*)NULL);
  }

  if (was_inited) EnsureInit(saved_baud, saved_tx, saved_rx, saved_tx_inv, saved_rx_inv);
}

inline bool AppendScanReport(Tcl_Interp* interp) {
  static constexpr uint32_t kHwBauds[] = {921600, 57600, 115200, 38400, 230400, 460800, 1000000, 19200, 9600};
  struct PolMode { bool tx_inv; bool rx_inv; };
  // Order: (0,0)=normal (centipede 921600), (1,0)=dragon (stock coco_devkitc w/ R14+R15 high @ 57600!), (1,1)=invert, (0,1)=rx_invert
  static constexpr PolMode kPols[] = {{false, false}, {true, false}, {true, true}, {false, true}};
  struct PinPair { uint tx; uint rx; const char* desc; bool hw_uart0; };
  static constexpr PinPair kPairs[] = {
    {12, 13, "40c-bodged / centipede UART0 (TX12->IO33, RX13<-IO21/IO13)", true},
    {12, 14, "IO33/IO26 pair (TX12->IO33, RX14<-IO26 CASS_OUT)",           false},
    {12, 15, "IO33/IO25 UART0-aux (TX12->IO33, RX15<-IO25)",               true},
    {14, 13, "IO26/IO21 UART0-aux (TX14->IO26, RX13<-IO21)",               true},
    {13, 12, "reversed 12/13 (TX13, RX12)",                                false},
#if !defined(_BOARDS_CENTIPEDE32Z_H)
    { 9,  8, "40c-unbodged R1/R2 (TX9->IO33, RX8<-IO21)",                  false},
    { 8,  9, "40c-unbodged swapped (TX8, RX9)",                            false},
#endif
  };

  Tcl_AppendResult(interp, "=== FujiNet Link Auto-Scan ===\n", (char*)NULL);
  char line[220];
  bool found = false;
  uint best_tx = active_tx_pin, best_rx = active_rx_pin;
  uint32_t best_baud = active_baud;
  bool best_tx_inv = active_tx_invert, best_rx_inv = active_rx_invert;
  bool best_is_hw = true;
  int partial_notes = 0;

  // Wait 550ms before starting scan in case a prior GPIO probe glitched ESP32 RX (readTimeout=500ms).
  sleep_ms(550);

  for (size_t pi = 0; pi < sizeof(kPairs) / sizeof(kPairs[0]); pi++) {
    const PinPair& pp = kPairs[pi];
    size_t max_pols = pp.hw_uart0 ? 4 : 3;
    for (size_t poli = 0; poli < max_pols; poli++) {
      bool tx_inv = kPols[poli].tx_inv;
      bool rx_inv = kPols[poli].rx_inv;
      // Configure TX idle level first and settle if TX polarity changed on primary RX pin 12:
      if (pp.hw_uart0) {
        EnsureInit(kHwBauds[0], pp.tx, pp.rx, tx_inv, rx_inv);
        if (pp.tx == 12) sleep_ms(520);  // clear any 500ms readTimeout from previous polarity's idle transition
      }
      for (size_t bi = 0; bi < sizeof(kHwBauds) / sizeof(kHwBauds[0]); bi++) {
        uint32_t baud = kHwBauds[bi];
        if (!pp.hw_uart0 && baud > 115200) continue;
        if ((tx_inv != rx_inv) && baud != 57600 && baud != 115200 && baud != 38400 && baud != 921600) continue;

        byte t[6] = {0};
        uint got = 0;
        if (pp.hw_uart0) {
          EnsureInit(baud, pp.tx, pp.rx, tx_inv, rx_inv);
          got = Transact(&kOpTime, 1, t, 6, 45);
        } else {
          Deinit();
          got = DiagBitbangTransact(pp.tx, pp.rx, baud, tx_inv, rx_inv, &kOpTime, 1, t, 6, 45);
        }

        if (got == 6 && ValidOpTime(t)) {
          snprintf(line, sizeof line,
                   "  MATCH: TX=GPIO%u RX=GPIO%u baud=%lu polarity=%s (%s)\n"
                   "         OP_TIME -> %04d-%02d-%02d %02d:%02d:%02d\n",
                   pp.tx, pp.rx, (unsigned long)baud, PolarityName(tx_inv, rx_inv), pp.desc,
                   1900 + t[0], t[1], t[2], t[3], t[4], t[5]);
          Tcl_AppendResult(interp, line, (char*)NULL);
          if (!found) {
            found = true;
            best_tx = pp.tx; best_rx = pp.rx; best_baud = baud;
            best_tx_inv = tx_inv; best_rx_inv = rx_inv;
            best_is_hw = pp.hw_uart0;
          }
        } else if (got > 0 && partial_notes < 6) {
          bool non_trivial = false;
          for (uint k = 0; k < got; k++) if (t[k] != 0x00 && t[k] != 0xFF) non_trivial = true;
          if (non_trivial) {
            int n = snprintf(line, sizeof line,
                             "  partial (%uB) at TX=%u RX=%u baud=%lu %s:",
                             got, pp.tx, pp.rx, (unsigned long)baud, PolarityName(tx_inv, rx_inv));
            for (uint k = 0; k < got && n < (int)sizeof(line) - 5; k++)
              n += snprintf(line + n, sizeof(line) - n, " %02x", t[k]);
            snprintf(line + n, sizeof(line) - n, "\n");
            Tcl_AppendResult(interp, line, (char*)NULL);
            partial_notes++;
          }
        }
      }
      if (found) break;
    }
    if (found) break;
  }

  if (found && best_is_hw) {
    EnsureInit(best_baud, best_tx, best_rx, best_tx_inv, best_rx_inv);
    snprintf(line, sizeof line,
             "Active UART0 locked to: TX=GPIO%u RX=GPIO%u baud=%lu polarity=%s\n",
             active_tx_pin, active_rx_pin, (unsigned long)active_baud,
             PolarityName(active_tx_invert, active_rx_invert));
    Tcl_AppendResult(interp, line, (char*)NULL);
  } else if (!found) {
    EnsureInit(kDefaultBaud, kTxPin, kRxPin, false, false);
    Tcl_AppendResult(interp, "  NO DriveWire reply on any tested pin/baud/polarity combination.\n", (char*)NULL);
  }
  return found;
}

inline bool AppendStatusReport(Tcl_Interp* interp) {
  char line[192];
  snprintf(line, sizeof line, "=== FujiNet Status (TX=GPIO%u RX=GPIO%u %lu baud %s) ===\n",
           active_tx_pin, active_rx_pin, (unsigned long)active_baud,
           PolarityName(active_tx_invert, active_rx_invert));
  Tcl_AppendResult(interp, line, (char*)NULL);

  byte t[6] = {0};
  uint got = Transact(&kOpTime, 1, t, 6, 500);
  if (got < 6) {
    snprintf(line, sizeof line, "  OP_TIME: no answer (%u of 6 bytes)\n", got);
    Tcl_AppendResult(interp, line, (char*)NULL);
    return false;
  }
  snprintf(line, sizeof line, "  Clock:    %04d-%02d-%02d %02d:%02d:%02d\n",
           1900 + t[0], t[1], t[2], t[3], t[4], t[5]);
  Tcl_AppendResult(interp, line, (char*)NULL);

  const byte get_cfg[2] = {kOpFuji, kFujiGetAdapterConfig};
  const byte get_resp[2] = {kOpFuji, kFujiGetResponse};
  if (FujiCmd(get_cfg, 2, 2000)) {
    byte cfg[kAdapterConfigSize + 1];
    memset(cfg, 0, sizeof cfg);
    if (Transact(get_resp, 2, cfg, kAdapterConfigSize, 2000) == kAdapterConfigSize) {
      char ssid[34], host[65], ver[16];
      memcpy(ssid, cfg + 0, 33);   ssid[33] = 0;
      memcpy(host, cfg + 33, 64);  host[64] = 0;
      memcpy(ver,  cfg + 125, 15); ver[15] = 0;
      const byte* ip = cfg + 97;
      const byte* gw = cfg + 101;
      const byte* nm = cfg + 105;
      const byte* dns = cfg + 109;
      const byte* mac = cfg + 113;
      snprintf(line, sizeof line,
               "  Version:  %s\n"
               "  Hostname: %s\n"
               "  SSID:     %s\n"
               "  IP:       %u.%u.%u.%u  (GW %u.%u.%u.%u, Mask %u.%u.%u.%u, DNS %u.%u.%u.%u)\n"
               "  MAC:      %02X:%02X:%02X:%02X:%02X:%02X\n",
               ver, host, ssid,
               ip[0], ip[1], ip[2], ip[3],
               gw[0], gw[1], gw[2], gw[3],
               nm[0], nm[1], nm[2], nm[3],
               dns[0], dns[1], dns[2], dns[3],
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      Tcl_AppendResult(interp, line, (char*)NULL);
    }
  }

  const byte get_wifi[2] = {kOpFuji, kFujiGetWifiStatus};
  if (FujiCmd(get_wifi, 2, 2000)) {
    byte ws = 0;
    if (Transact(get_resp, 2, &ws, 1, 1000) == 1) {
      snprintf(line, sizeof line, "  WiFi:     %s (code %u)\n",
               ws == 3 ? "CONNECTED" : "not connected", (unsigned)ws);
      Tcl_AppendResult(interp, line, (char*)NULL);
    }
  }

  const byte read_hosts[2] = {kOpFuji, kFujiReadHostSlots};
  if (FujiCmd(read_hosts, 2, 2000)) {
    byte hosts[kHostSlots * kHostSlotSize];
    if (Transact(get_resp, 2, hosts, sizeof hosts, 2000) == sizeof hosts) {
      Tcl_AppendResult(interp, "  Host slots:\n", (char*)NULL);
      for (uint i = 0; i < kHostSlots; i++) {
        const char* h = (const char*)(hosts + i * kHostSlotSize);
        if (h[0]) {
          snprintf(line, sizeof line, "    [%u] %.32s\n", i + 1, h);
          Tcl_AppendResult(interp, line, (char*)NULL);
        }
      }
    }
  }

  const byte read_devs[2] = {kOpFuji, kFujiReadDeviceSlots};
  if (FujiCmd(read_devs, 2, 2000)) {
    byte devs[kDeviceSlots * kDeviceSlotSize];
    if (Transact(get_resp, 2, devs, sizeof devs, 2000) == sizeof devs) {
      Tcl_AppendResult(interp, "  Device slots:\n", (char*)NULL);
      for (uint i = 0; i < kDeviceSlots; i++) {
        const byte* d = devs + i * kDeviceSlotSize;
        if (d[0] < 8 && d[2]) {
          snprintf(line, sizeof line, "    [slot %u] host=%u mode=%s %.36s\n",
                   i + 1, (unsigned)(d[0] + 1), d[1] == 2 ? "RW" : "RO", (const char*)(d + 2));
        } else {
          snprintf(line, sizeof line, "    [slot %u] (empty)\n", i + 1);
        }
        Tcl_AppendResult(interp, line, (char*)NULL);
      }
    }
  }
  return true;
}

}  // namespace becker

byte IN_RAM becker_read_status(uint) {
  return becker::rx_ring.size() ? 0x02 : 0x00;
}
void IN_RAM becker_write_status(uint, byte) {}

byte IN_RAM becker_read_data(uint) {
  byte b;
  if (!becker::rx_ring.pop(b)) {
    becker::stat_empty_reads++;
    return 0xFF;
  }
  return b;
}
void IN_RAM becker_write_data(uint, byte d) {
  if (!becker::tx_ring.push(d)) becker::stat_tx_drops++;
}

struct BeckerPort {
  static constexpr uint16_t kDefaultBase = 0xFF41;
  static void Install(uint16_t base = kDefaultBase) {
    uint32_t baud = becker::active_baud;
    uint tx_pin = becker::active_tx_pin;
    uint rx_pin = becker::active_rx_pin;
    bool tx_inv = becker::active_tx_invert;
    bool rx_inv = becker::active_rx_invert;

    const char* baud_str = g_port_settings.Get("dw.baud");
    uint32_t parsed = 0;
    if (baud_str && ParsePositiveInt(baud_str, &parsed)) baud = parsed;

    const char* tx_str = g_port_settings.Get("dw.tx");
    if (tx_str && ParsePositiveInt(tx_str, &parsed) && becker::IsValidUart0Tx(parsed)) tx_pin = parsed;

    const char* rx_str = g_port_settings.Get("dw.rx");
    if (rx_str && ParsePositiveInt(rx_str, &parsed) && becker::IsValidUart0Rx(parsed)) rx_pin = parsed;

    const char* over_str = g_port_settings.Get("dw.over");
    if (over_str) {
      if (!strcmp(over_str, "bitbang")) { tx_inv = true; rx_inv = true; }
      else if (!strcmp(over_str, "uart")) { tx_inv = false; rx_inv = false; }
    }
    const char* pol_str = g_port_settings.Get("dw.polarity");
    if (pol_str) {
      (void)becker::ParsePolarity(pol_str, &tx_inv, &rx_inv);
    }

    becker::EnsureInit(baud, tx_pin, rx_pin, tx_inv, rx_inv);
    becker::Flush();
    becker::port_base = base;
    becker::active = true;
    IoClaim(base, base + 1, "becker");
    IoSet(base + 0, becker_read_status, becker_write_status);
    IoSet(base + 1, becker_read_data,   becker_write_data);
  }
};

inline int fujinet_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  static char result[200];
  if (argc > 1 && !strcmp(argv[1], "help")) {
    Tcl_AppendResult(interp,
        "fujinet                  query FujiNet clock (OP_TIME) on active UART settings\n"
        "fujinet diag             full hardware pin check + baud/polarity auto-scan + status\n"
        "fujinet pins             probe hat GPIO electrical states (HIGH/LOW/FLOAT/TOGGLE)\n"
        "fujinet scan             sweep pin pairs, polarities (normal/dragon/invert) & bauds\n"
        "fujinet status           show FujiNet version, WiFi, IP, host slots & device slots\n"
        "fujinet config ?baud? ?normal|dragon|invert|rx_invert? ?tx rx?  show/set UART0\n"
        "fujinet mount ?1..4?     mount FujiNet device slot N (default 1)\n"
        "fujinet raw <hex> ?want? ?ms?  send hex bytes and return hex reply\n"
        "fujinet pulse <gpio>     pulse a non-driven hat GPIO low for 200ms (continuity test)\n"
        "fujinet drive <gpio> <0|1|z>   drive a hat GPIO at 2mA (0/1) or release to hi-Z (z)\n"
        "fujinet sniff ?ms?       listen passively for UART bytes (default 2000ms)\n"
        "fujinet stats            show byte and error counters",
        (char*)NULL);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "pins")) {
    becker::AppendPinsReport(interp);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "scan")) {
    becker::AppendScanReport(interp);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "status")) {
    return becker::AppendStatusReport(interp) ? TCL_OK : TCL_ERROR;
  }
  if (argc > 1 && !strcmp(argv[1], "diag")) {
    becker::AppendPinsReport(interp);
    if (becker::AppendScanReport(interp)) {
      becker::AppendStatusReport(interp);
    }
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "config")) {
    uint32_t baud = becker::active_baud;
    bool tx_inv = becker::active_tx_invert, rx_inv = becker::active_rx_invert;
    uint tx = becker::active_tx_pin, rx = becker::active_rx_pin;
    if (argc > 2) {
      int b = atoi(argv[2]);
      if (b <= 0) {
        Tcl_SetResult(interp, (char*)"baud must be positive", TCL_STATIC);
        return TCL_ERROR;
      }
      baud = (uint32_t)b;
    }
    if (argc > 3) {
      if (!becker::ParsePolarity(argv[3], &tx_inv, &rx_inv)) {
        Tcl_SetResult(interp, (char*)"polarity must be normal, dragon (tx_invert), invert, or rx_invert", TCL_STATIC);
        return TCL_ERROR;
      }
    }
    if (argc > 5) {
      uint ptx = (uint)atoi(argv[4]), prx = (uint)atoi(argv[5]);
      if (!becker::IsValidUart0Tx(ptx) || !becker::IsValidUart0Rx(prx)) {
        Tcl_SetResult(interp, (char*)"UART0 hat pins: TX in {12,14,16,18}, RX in {13,15,17,19}", TCL_STATIC);
        return TCL_ERROR;
      }
      tx = ptx; rx = prx;
    }
    becker::EnsureInit(baud, tx, rx, tx_inv, rx_inv);
    snprintf(result, sizeof result, "TX=GPIO%u RX=GPIO%u baud=%lu polarity=%s",
             becker::active_tx_pin, becker::active_rx_pin,
             (unsigned long)becker::active_baud,
             becker::PolarityName(becker::active_tx_invert, becker::active_rx_invert));
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "raw")) {
    if (argc < 3) {
      Tcl_SetResult(interp, (char*)"usage: fujinet raw <hexbytes> ?want? ?ms?", TCL_STATIC);
      return TCL_ERROR;
    }
    byte out[64];
    uint n = 0;
    const char* s = argv[2];
    while (*s && n < sizeof(out)) {
      if (*s == ' ' || *s == ':') { s++; continue; }
      auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = hexv(s[0]);
      int lo = s[1] ? hexv(s[1]) : -1;
      if (hi < 0 || lo < 0) {
        Tcl_SetResult(interp, (char*)"invalid hex string", TCL_STATIC);
        return TCL_ERROR;
      }
      out[n++] = (byte)((hi << 4) | lo);
      s += 2;
    }
    uint want = argc > 3 ? (uint)atoi(argv[3]) : 32;
    if (want > 256) want = 256;
    uint ms = argc > 4 ? (uint)atoi(argv[4]) : 300;
    byte in[256];
    uint got = becker::Transact(out, n, in, want, ms);
    snprintf(result, sizeof result, "rx %u bytes:", got);
    Tcl_AppendResult(interp, result, (char*)NULL);
    char bhex[8];
    for (uint i = 0; i < got; i++) {
      snprintf(bhex, sizeof bhex, " %02x", in[i]);
      Tcl_AppendResult(interp, bhex, (char*)NULL);
    }
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "sniff")) {
    uint ms = argc > 2 ? (uint)atoi(argv[2]) : 2000;
    if (ms > 15000) ms = 15000;
    becker::EnsureInit(becker::active_baud, becker::active_tx_pin, becker::active_rx_pin,
                       becker::active_tx_invert, becker::active_rx_invert);
    becker::Flush();
    byte in[128];
    uint got = 0;
    uint64_t deadline = time_us_64() + (uint64_t)ms * 1000ULL;
    while (time_us_64() < deadline) {
      byte b;
      if (becker::rx_ring.pop(b) && got < sizeof(in)) in[got++] = b;
    }
    snprintf(result, sizeof result, "sniffed %u ms on RX=GPIO%u (%lu baud %s): %u bytes:",
             ms, becker::active_rx_pin, (unsigned long)becker::active_baud,
             becker::PolarityName(becker::active_tx_invert, becker::active_rx_invert), got);
    Tcl_AppendResult(interp, result, (char*)NULL);
    char bhex[8];
    for (uint i = 0; i < got; i++) {
      snprintf(bhex, sizeof bhex, " %02x", in[i]);
      Tcl_AppendResult(interp, bhex, (char*)NULL);
    }
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "mount")) {
    int n = argc > 2 ? atoi(argv[2]) : 1;
    if (n < 1 || n > (int)becker::kDeviceSlots) {
      Tcl_SetResult(interp, (char*)"device slots are 1 to 4", TCL_STATIC);
      return TCL_ERROR;
    }
    char name[40];
    const char* err = becker::MountSlot((uint)(n - 1), name, sizeof name);
    if (err) snprintf(result, sizeof result, "slot %d: %s", n, err);
    else snprintf(result, sizeof result, "slot %d: mounted %s", n, name);
    Tcl_SetResult(interp, result, TCL_STATIC);
    return err ? TCL_ERROR : TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "pulse")) {
    if (argc < 3) {
      Tcl_SetResult(interp, (char*)"usage: fujinet pulse <gpio>", TCL_STATIC);
      return TCL_ERROR;
    }
    uint g = (uint)atoi(argv[2]);
    bool allowed = false;
    for (size_t i = 0; i < sizeof(becker::kHatPins) / sizeof(becker::kHatPins[0]); i++) {
      if (becker::kHatPins[i].gpio == g) allowed = true;
    }
    if (!allowed) {
      Tcl_SetResult(interp, (char*)"pin is not a safe hat GPIO", TCL_STATIC);
      return TCL_ERROR;
    }
    becker::Deinit();
    becker::PinState st = becker::ProbeSinglePin(g);
    if (st == becker::PIN_HIGH) {
      snprintf(result, sizeof result, "refusing to pulse GPIO%u: externally driven HIGH", g);
      Tcl_SetResult(interp, result, TCL_STATIC);
      return TCL_ERROR;
    }
    gpio_init(g);
    gpio_put(g, 0);
    gpio_set_dir(g, GPIO_OUT);
    sleep_ms(200);
    gpio_put(g, 1);
    sleep_ms(80);
    gpio_set_dir(g, GPIO_IN);
    gpio_pull_up(g);
    snprintf(result, sizeof result, "pulsed GPIO%u low for 200ms", g);
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "drive")) {
    if (argc < 4) {
      Tcl_SetResult(interp, (char*)"usage: fujinet drive <gpio> <0|1|z>", TCL_STATIC);
      return TCL_ERROR;
    }
    uint g = (uint)atoi(argv[2]);
    bool allowed = false;
    for (size_t i = 0; i < sizeof(becker::kHatPins) / sizeof(becker::kHatPins[0]); i++) {
      if (becker::kHatPins[i].gpio == g) allowed = true;
    }
    if (!allowed) {
      Tcl_SetResult(interp, (char*)"pin is not a safe hat GPIO", TCL_STATIC);
      return TCL_ERROR;
    }
    becker::Deinit();
    char mode = argv[3][0];
    gpio_init(g);
    gpio_set_drive_strength(g, GPIO_DRIVE_STRENGTH_2MA);
    if (mode == 'z' || mode == 'Z') {
      gpio_set_dir(g, GPIO_IN);
      gpio_disable_pulls(g);
      snprintf(result, sizeof result, "GPIO%u -> Z", g);
    } else if (mode == '0' || mode == '1') {
      gpio_disable_pulls(g);
      gpio_put(g, mode == '1' ? 1 : 0);
      gpio_set_dir(g, GPIO_OUT);
      snprintf(result, sizeof result, "GPIO%u -> %c", g, mode);
    } else {
      Tcl_SetResult(interp, (char*)"mode must be 0, 1, or z", TCL_STATIC);
      return TCL_ERROR;
    }
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "stats")) {
    snprintf(result, sizeof result,
             "rx %lu tx %lu rx_drops %lu tx_drops %lu overruns %lu empty_reads %lu",
             (unsigned long)becker::stat_rx_bytes, (unsigned long)becker::stat_tx_bytes,
             (unsigned long)becker::stat_rx_drops, (unsigned long)becker::stat_tx_drops,
             (unsigned long)becker::stat_overruns, (unsigned long)becker::stat_empty_reads);
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_OK;
  }
  if (argc > 1) {
    Tcl_AppendResult(interp, "unknown subcommand \"", argv[1],
                     "\": try 'fujinet help' or 'fujinet diag'", (char*)NULL);
    return TCL_ERROR;
  }
  byte t[6];
  uint got = becker::Transact(&becker::kOpTime, 1, t, sizeof t, 1000);
  if (got < sizeof t || !becker::ValidOpTime(t)) {
    snprintf(result, sizeof result,
             "no answer from the FujiNet (%u of 6 bytes; GPIO %u TX, %u RX, %u baud, %s)",
             got, becker::active_tx_pin, becker::active_rx_pin,
             (unsigned)becker::active_baud,
             becker::PolarityName(becker::active_tx_invert, becker::active_rx_invert));
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_ERROR;
  }
  snprintf(result, sizeof result, "FujiNet says it is %04d-%02d-%02d %02d:%02d:%02d",
           1900 + t[0], t[1], t[2], t[3], t[4], t[5]);
  Tcl_SetResult(interp, result, TCL_STATIC);
  return TCL_OK;
}
