#pragma once
// becker.h -- DriveWire / FujiNet Becker port over UART0 (GPIO 12 TX, GPIO 13 RX).
//
// Default address in enhancement map `e1`: 0xFF41 (status), 0xFF42 (data);
// on `jamesbrown`: 0xFF45..0xFF46.
// Status read: bit 1 (0x02) set when RX data is waiting, else 0x00.
// Data read:   next RX byte (0xFF if empty); Data write: enqueue TX byte to UART.
//
// Core 1 touches only lock-free SRAM rings (`rx_ring`, `tx_ring`).  On core 0, `UartIrq` fills
// `rx_ring` and `Pump()` drains `tx_ring` onto UART0.  Baud defaults to 921600 (FujiNet high-speed
// UART) and honors `dw.baud` from `g_port_settings` when specified (`with ..., becker, dw baud 115200`).

#include <hardware/irq.h>
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
inline uint16_t port_base = 0xFF41;
inline bool hw_inited = false;
inline bool active = false;

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

inline void EnsureInit(uint32_t baud = kDefaultBaud) {
  if (!hw_inited) {
    hw_inited = true;
    gpio_set_function(kTxPin, UART_FUNCSEL_NUM(BECKER_UART, kTxPin));
    gpio_set_function(kRxPin, UART_FUNCSEL_NUM(BECKER_UART, kRxPin));
    gpio_pull_up(kRxPin);
    uart_init(BECKER_UART, baud);
    uart_set_format(BECKER_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(BECKER_UART, false, false);
    uart_set_fifo_enabled(BECKER_UART, true);
    irq_set_exclusive_handler(UART_IRQ_NUM(BECKER_UART), UartIrq);
    irq_set_enabled(UART_IRQ_NUM(BECKER_UART), true);
    uart_set_irqs_enabled(BECKER_UART, true, false);
    active_baud = baud;
  } else if (baud != active_baud) {
    uart_set_baudrate(BECKER_UART, baud);
    active_baud = baud;
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

inline void Flush() {
  byte b;
  while (tx_ring.pop(b)) {}
  while (rx_ring.pop(b)) {}
}

inline uint Transact(const byte* out, uint n, byte* in, uint want, uint ms) {
  EnsureInit(active_baud);
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

constexpr byte kOpFuji = 0xE2;
constexpr byte kFujiGetResponse = 0x01, kFujiGetError = 0x02;
constexpr byte kFujiReadDeviceSlots = 0xF2, kFujiMountHost = 0xF9, kFujiMountImage = 0xF8;
constexpr byte kFujiOk = 0x01;
constexpr uint kDeviceSlotSize = 38;
constexpr uint kDeviceSlots = 4;

inline bool FujiCmd(const byte* req, uint n) {
  byte status = 0;
  Transact(req, n, nullptr, 0, 50);
  const byte get_error[2] = {kOpFuji, kFujiGetError};
  return Transact(get_error, 2, &status, 1, 15000) == 1 && status == kFujiOk;
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
    uint32_t baud = becker::kDefaultBaud;
    const char* baud_str = g_port_settings.Get("dw.baud");
    uint32_t parsed = 0;
    if (baud_str && ParsePositiveInt(baud_str, &parsed)) {
      baud = parsed;
    }
    becker::EnsureInit(baud);
    becker::Flush();
    becker::port_base = base;
    becker::active = true;
    IoClaim(base, base + 1, "becker");
    IoSet(base + 0, becker_read_status, becker_write_status);
    IoSet(base + 1, becker_read_data,   becker_write_data);
  }
};

inline int fujinet_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  static char result[160];
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
  if (argc > 1 && !strcmp(argv[1], "stats")) {
    snprintf(result, sizeof result,
             "rx %lu tx %lu rx_drops %lu tx_drops %lu overruns %lu empty_reads %lu",
             (unsigned long)becker::stat_rx_bytes, (unsigned long)becker::stat_tx_bytes,
             (unsigned long)becker::stat_rx_drops, (unsigned long)becker::stat_tx_drops,
             (unsigned long)becker::stat_overruns, (unsigned long)becker::stat_empty_reads);
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_OK;
  }
  const byte op_time = 0x23;
  byte t[6];
  uint got = becker::Transact(&op_time, 1, t, sizeof t, 1000);
  if (got < sizeof t) {
    snprintf(result, sizeof result,
             "no answer from the FujiNet (%u of 6 bytes; GPIO %u TX, %u RX, %u baud)",
             got, becker::kTxPin, becker::kRxPin, (unsigned)becker::active_baud);
    Tcl_SetResult(interp, result, TCL_STATIC);
    return TCL_ERROR;
  }
  snprintf(result, sizeof result, "FujiNet says it is %04d-%02d-%02d %02d:%02d:%02d",
           1900 + t[0], t[1], t[2], t[3], t[4], t[5]);
  Tcl_SetResult(interp, result, TCL_STATIC);
  return TCL_OK;
}
