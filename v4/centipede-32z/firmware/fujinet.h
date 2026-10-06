#ifndef CENTIPEDE_FIRMWARE_FUJINET_H_
#define CENTIPEDE_FIRMWARE_FUJINET_H_

#include <hardware/irq.h>
#include <hardware/uart.h>

// Becker-port serial bridge to a FujiNet.
//
// Enabled at boot by centipede_config.fujinet (the FujiNet box in
// rc/mode1.tcl, or hold F at restart for rc/mode70.tcl).  When it is on,
// foreground() routes SCS reads/writes at $FF41 (status) and $FF42 (data)
// here, and serves HDB-DOS at $C000 (in place of disk11, and even when the
// disk11 box is off).
//
// foreground() (core1) is the hard-real-time bus-servicing loop: it runs
// with interrupts disabled and must answer every single 6809 bus cycle.
// The RP2350's hardware UART FIFOs are only 32 bytes deep, and with hardware
// flow control disabled, uart_putc/uart_getc block on the wire's own baud
// rate once a command outruns that FIFO (e.g. setting a WiFi SSID) -- a
// multi-cycle stall there is enough to desync the PIO bus dispatch. So
// foreground() only ever touches these cross-core ring buffers; the
// background core (core0) moves bytes between them and the real UART.
//
// Receiving can't wait for the background loop: at 921600 baud the 32-byte
// RX FIFO fills in ~350us, and the loop can be busy far longer than that
// (e.g. sending the bus trace to tether over USB), which overruns the FIFO
// and loses bytes in the middle of a DriveWire sector.  So RX is drained by
// a UART interrupt on core0.  Sending has no such deadline and is left to
// PumpFujinet.
#define FUJINET_UART_ID uart0
#define FUJINET_BAUD_RATE (115200 * 8)
#define FUJINET_TX_PIN 12
#define FUJINET_RX_PIN 13

// Becker-port logging to the tether window, turned on by
// centipede_config.trace_becker (the "Trace Becker Port" box in
// rc/mode1.tcl).
//
// Lines look like
//     [BK  12345ms W] 01 d2 00 ...          bytes the 6809 wrote to $FF42
//     [BK  12350ms R] (412 polls) 01 00 ... bytes it read from $FF42
// "(N polls)" is how many times $FF41 said "nothing yet" before data
// arrived.  Drop counters are printed as "!! BK ..." whenever one grows.
//
// The foreground only pushes one fg2bg word per data byte (status polls
// are just counted); all formatting happens on the background core.
// The drop counters below are reported whenever FujiNet is on.

// Copy of centipede_config.trace_becker, set by foreground() before its
// bus loop.  Only core1 reads it.
bool becker_trace = false;

// Set by foreground() (core1), read by PumpFujinet() (core0).
std::atomic<bool> fujinet_inited{false};
CrossCoreFIFO<byte, 1024> fujinet_tx_fifo;
CrossCoreFIFO<byte, 1024> fujinet_rx_fifo;

// Things that lose or corrupt becker bytes.  Written by one core, only
// read by the other.
volatile uint32_t fujinet_tx_drops = 0;     // fg: tx_fifo full, 6809 byte lost
volatile uint32_t fujinet_rx_drops = 0;     // bg: rx_fifo full, FujiNet byte lost
volatile uint32_t fujinet_empty_reads = 0;  // fg: $FF42 read with nothing there
volatile uint32_t fujinet_overruns = 0;     // bg: UART RX FIFO overran, bytes lost

// Background-side formatter state.
namespace becker_log {
char line[128];
int len = 0;
char dir = 0;  // 'R', 'W', or 0 if no line is open.
int count = 0;
uint32_t polls = 0;  // From the most recent FG2BG_BECKER_POLLS.
uint64_t last_byte_us = 0;
uint32_t seen_tx_drops = 0, seen_rx_drops = 0, seen_empty_reads = 0;
uint32_t seen_overruns = 0;

void Flush() {
  if (len) {
    line[len] = 0;
    cobs_printf("%s\n", line);
  }
  len = 0;
  dir = 0;
  count = 0;
}

void Byte(char d, byte b) {
  if (dir != d || count >= 16) {
    Flush();
    dir = d;
    len = snprintf(line, sizeof line, "[BK %6lums %c]",
                   (unsigned long)(time_us_64() / 1000), d);
    if (d == 'R' && polls) {
      len += snprintf(line + len, sizeof line - len, " (%lu polls)",
                      (unsigned long)polls);
    }
    polls = 0;
  }
  len += snprintf(line + len, sizeof line - len, " %02x", b);
  count++;
  last_byte_us = time_us_64();
}

// Called from drain_task between chores, so a partial line shows up once
// the port has been quiet for 5ms instead of waiting for 16 bytes.
void Idle() {
  if (len && time_us_64() - last_byte_us > 5000) Flush();
  if (seen_tx_drops != fujinet_tx_drops ||
      seen_rx_drops != fujinet_rx_drops ||
      seen_empty_reads != fujinet_empty_reads ||
      seen_overruns != fujinet_overruns) {
    seen_tx_drops = fujinet_tx_drops;
    seen_rx_drops = fujinet_rx_drops;
    seen_empty_reads = fujinet_empty_reads;
    seen_overruns = fujinet_overruns;
    cobs_printf("!! BK tx_drops=%lu rx_drops=%lu empty_reads=%lu overruns=%lu\n",
                (unsigned long)seen_tx_drops, (unsigned long)seen_rx_drops,
                (unsigned long)seen_empty_reads, (unsigned long)seen_overruns);
  }
}
}  // namespace becker_log

// Called from drain_task for the FG2BG_BECKER_* chores.
void BeckerLogChore(uint chore_num, uint chore) {
  switch (chore_num) {
    case FG2BG_BECKER_READ:
      becker_log::Byte('R', chore & 0xFF);
      break;
    case FG2BG_BECKER_WRITE:
      becker_log::Byte('W', chore & 0xFF);
      break;
    case FG2BG_BECKER_POLLS:
      becker_log::polls = (chore >> 8) & 0xFFFF;
      break;
  }
}

void BeckerLogIdle() { becker_log::Idle(); }

// UART RX interrupt, on core0.  This is the only producer for rx_fifo.
void __not_in_flash_func(FujinetUartIrq)() {
  uart_hw_t* const hw = uart_get_hw(FUJINET_UART_ID);
  if (hw->rsr & UART_UARTRSR_OE_BITS) {
    fujinet_overruns++;
    hw->rsr = UART_UARTRSR_BITS;  // Any write clears the error bits.
  }
  while (uart_is_readable(FUJINET_UART_ID)) {
    byte b = (byte)uart_getc(FUJINET_UART_ID);
    if (!fujinet_rx_fifo.push(b)) fujinet_rx_drops++;  // drop, never block
  }
}

// Called from the background core's scheduler loop.
void PumpFujinet() {
  if (!fujinet_inited) return;

  // InitFujinet runs on core1, which has interrupts off, so the RX
  // interrupt is hooked up here, the first time core0 sees FujiNet on.
  static bool irq_installed = false;
  if (!irq_installed) {
    const uint irq = UART_IRQ_NUM(FUJINET_UART_ID);
    irq_set_exclusive_handler(irq, FujinetUartIrq);
    irq_set_enabled(irq, true);
    uart_set_irqs_enabled(FUJINET_UART_ID, true, false);
    irq_installed = true;
  }

  byte d;
  while (uart_is_writable(FUJINET_UART_ID) && fujinet_tx_fifo.pop(d)) {
    uart_putc_raw(FUJINET_UART_ID, d);
  }
}

template <typename T>
struct DontFujinet {
  static void InitFujinet() {}
  static byte ReadFujinet(uint a) { return 0xFF; }
  static void WriteFujinet(uint a, byte d) {}
  static byte GetFujinetStatus() { return 0; }
};

template <typename T>
struct DoFujinet {
  // Only called once centipede_config.fujinet is known, so the UART pins
  // are left alone unless FujiNet was chosen.
  static void InitFujinet() {
    if (fujinet_inited) return;

    gpio_set_function(FUJINET_TX_PIN,
                      UART_FUNCSEL_NUM(FUJINET_UART_ID, FUJINET_TX_PIN));
    gpio_set_function(FUJINET_RX_PIN,
                      UART_FUNCSEL_NUM(FUJINET_UART_ID, FUJINET_RX_PIN));

    uart_init(FUJINET_UART_ID, FUJINET_BAUD_RATE);
    uart_set_format(FUJINET_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(FUJINET_UART_ID, false, false);

    fujinet_inited = true;
  }

  // Non-blocking: the 6809 only reads data once GetFujinetStatus() has
  // reported a byte is available, so rx_fifo should always have one.
  // These three run on the foreground for a single bus cycle, so they must
  // be inlined into it (in RAM): an out-of-line copy lands in flash, and an
  // XIP cache miss there stalls past the end of the 6809 cycle.
  FORCE_INLINE static byte ReadFujinet(uint a) {
    byte d;
    if (!fujinet_rx_fifo.pop(d)) {
      fujinet_empty_reads++;
      d = 0xFF;
    }
    if (becker_trace) PUSH_TO_BG(FG2BG_BECKER_READ, 0, d);
    return d;
  }

  // Non-blocking: queues for PumpFujinet to actually put on the wire.
  FORCE_INLINE static void WriteFujinet(uint a, byte d) {
    if (!fujinet_tx_fifo.push(d)) fujinet_tx_drops++;
    if (becker_trace) PUSH_TO_BG(FG2BG_BECKER_WRITE, 0, d);
  }

  FORCE_INLINE static byte GetFujinetStatus() {
    if (!fujinet_inited) return 0;
    byte status = fujinet_rx_fifo.size() > 0 ? 2 : 0;  // bit1 = RX ready
    if (becker_trace) {
      // Count "not ready" polls; report the count once data is ready.
      static uint polls = 0;
      if (status) {
        if (polls) PUSH_TO_BG(FG2BG_BECKER_POLLS, polls > 0xFFFF ? 0xFFFF : polls, 0);
        polls = 0;
      } else {
        polls++;
      }
    }
    return status;
  }
};

#endif  // CENTIPEDE_FIRMWARE_FUJINET_H_
