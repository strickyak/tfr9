// main.cpp — TurboLab firmware for TFR/911H (RP2350B + 6309E)
//
// Self-contained subset firmware:
// - No TCL
// - No LittleFS
// - Pure tethered operation with RPC-based handshake & image upload
// - Red-page ($FF04..$FFEF) protection
// - Zero interrupt vector ($0000) abort/fault
// - Dynamic trace filtering, cycle/time triggers, and max limits
// - Immediate core dump transmission on Fault

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/sio.h>
#include <hardware/timer.h>
#include <pico/bootrom.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hamster.pio.h"
#include "circbuf.h"
#include "cobs.h"
#include "cross-core.h"
#include "pcb.h"

#define FORCE_INLINE inline __attribute__((always_inline))
#define IN_RAM __not_in_flash("turbolab")
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

using byte = uint8_t;
using uint = unsigned int;

// ═══════════════════════════════════════════════════════════════════
// Protocol Constants
// ═══════════════════════════════════════════════════════════════════
constexpr byte C_NOP          = 0;
constexpr byte C_FAULT        = 188;
constexpr byte C_CORE_DUMP    = 189;
constexpr byte C_PUTCHAR      = 193;
constexpr byte C_RESTARTED    = 194;
constexpr byte C_TRACE_CYCLES = 200;

constexpr byte T_REFLASH_NOW_PLEASE = 175;
constexpr byte T_RESTART_NOW_PLEASE = 177;
constexpr byte T_CONSOLE_LINE       = 179;
constexpr byte T_PICO_RPC           = 181;

// Fault Reason Codes
constexpr byte FAULT_RED_PAGE    = 1;
constexpr byte FAULT_ZERO_VECTOR = 2;
constexpr byte FAULT_MAX_CYCLES  = 3;
constexpr byte FAULT_MAX_TIME    = 4;

// Trace Event Kinds
constexpr byte KIND_IDLE        = 0;  // '-'
constexpr byte KIND_FIC         = 1;  // 'x'
constexpr byte KIND_OPCODE_CONT = 2;  // '+'
constexpr byte KIND_READ        = 3;  // 'r'
constexpr byte KIND_WRITE       = 4;  // 'w'
constexpr byte KIND_IRQ         = 5;  // 'i' IRQ
constexpr byte KIND_FIRQ        = 6;  // 'i' FIRQ
constexpr byte KIND_NMI         = 7;  // 'i' NMI
constexpr byte KIND_RTI         = 8;  // 'i' RTI
constexpr byte KIND_SWI2        = 9;  // 't' SWI2

// Trace Flag Bitmasks
constexpr uint8_t TRACE_FLAG_X    = 1 << 0;
constexpr uint8_t TRACE_FLAG_R    = 1 << 1;
constexpr uint8_t TRACE_FLAG_W    = 1 << 2;
constexpr uint8_t TRACE_FLAG_I    = 1 << 3;
constexpr uint8_t TRACE_FLAG_T    = 1 << 4;
constexpr uint8_t TRACE_FLAG_PLUS = 1 << 5;

// ═══════════════════════════════════════════════════════════════════
// TFR911H Pin Assignments
// ═══════════════════════════════════════════════════════════════════
constexpr uint RESET = 20;
constexpr uint NMI   = 21;
constexpr uint IRQ   = 22;
constexpr uint FIRQ  = 23;
constexpr uint HALT  = 24;
constexpr uint LED   = 25;
constexpr uint LIC   = 26;
constexpr uint AVMA  = 27;
constexpr uint BS    = 28;
constexpr uint E     = 29;
constexpr uint Q     = 30;
constexpr uint R_W   = 31;

// ═══════════════════════════════════════════════════════════════════
// Memory & State
// ═══════════════════════════════════════════════════════════════════
alignas(4) byte ram[65536];

struct TraceRecord {
  uint64_t cycle;
  uint16_t addr;
  uint8_t data;
  uint8_t kind;
};

CrossCoreFIFO<TraceRecord, 8192> fg2bg_trace;
CrossCoreFIFO<byte, 8192> fg2bg_chars;

// Console line input ring buffer for 6309 ACIA
CircBuf<byte, 4096> term_input;

// Configuration set by Tether via RPC
volatile uint8_t trace_flags = 0;
volatile uint64_t trigger_cycle = 0;
volatile uint64_t trigger_time_us = 0;
volatile uint64_t max_cycles = 0;
volatile uint64_t max_time_us = 0;

// Execution state
enum FirmwareState {
  STATE_WAIT_CONFIG,
  STATE_RUNNING,
  STATE_FAULT,
  STATE_HALTED
};

volatile FirmwareState fw_state = STATE_WAIT_CONFIG;
volatile bool foreground_running = false;
volatile bool fault_triggered = false;
volatile byte fault_reason = 0;
volatile uint64_t fault_cycle = 0;
volatile uint16_t fault_addr = 0;
volatile byte fault_data = 0;

uint64_t start_time_us = 0;

// Turbo9Sim emulated ACIA registers ($FF00..$FF03)
constexpr byte SIM_TIMER_BIT = 0x01;
constexpr byte SIM_RX_BIT    = 0x02;

volatile byte sim_status_reg = 0;
volatile byte sim_control_reg = 0;
volatile byte sim_last_char_tx = 0;

struct repeating_timer timer60hz_data;
volatile bool timer60hz_running = false;
volatile bool waiting_for_tether_packet = true;
volatile uint64_t last_beacon_us = 0;

bool IN_RAM Timer60HzCallback(repeating_timer_t* rt) {
  sim_status_reg |= SIM_TIMER_BIT;
  return true;
}

// ═══════════════════════════════════════════════════════════════════
// Pin & Hardware Helpers
// ═══════════════════════════════════════════════════════════════════
FORCE_INLINE void IN_RAM LedOn()  { gpio_put(LED, 1); }
FORCE_INLINE void IN_RAM LedOff() { gpio_put(LED, 0); }

// HALT is open-drain: assert via direction OUT (latch=0), release via IN
FORCE_INLINE void IN_RAM HaltOn()  { gpio_set_dir(HALT, GPIO_OUT); }
FORCE_INLINE void IN_RAM HaltOff() { gpio_set_dir(HALT, GPIO_IN);  }

// IRQ is open-drain
FORCE_INLINE void IN_RAM AssertIRQ()  { gpio_set_dir(IRQ, GPIO_OUT); }
FORCE_INLINE void IN_RAM ReleaseIRQ() { gpio_set_dir(IRQ, GPIO_IN);  }

void InitializePins() {
  for (int i = 0; i < 48; i++) {
    gpio_init(i);
    switch (i) {
      case E:
      case Q:
      case LED:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
        break;

      case RESET:
      case NMI:
      case IRQ:
      case FIRQ:
      case HALT:
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);            // Latch 0
        gpio_set_dir(i, GPIO_IN);  // Input (released via pullup)
        gpio_set_pulls(i, true, false);
        break;

      default:
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
        break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Core 1: Foreground Real-Time Bus Engine
// ═══════════════════════════════════════════════════════════════════
void IN_RAM foreground_loop() {
  volatile sio_hw_t* hw = (volatile sio_hw_t*)sio_hw;

  uint64_t cycles = 0;
  uint prev_late_pins = 0;
  uint16_t prev_addr = 0;
  byte prev_kind = KIND_IDLE;
  bool prev_irq_needed = false;

  LedOff();
  foreground_running = true;

  while (true) {
    if (UNLIKELY(fault_triggered)) {
      HaltOn();
      break;
    }

    // Check term_input to update SIM_RX_BIT
    if (!term_input.Empty()) {
      sim_status_reg |= SIM_RX_BIT;
    }

    // Check IRQ status
    bool irq_needed = (sim_status_reg & sim_control_reg) != 0;
    if (irq_needed != prev_irq_needed) {
      if (irq_needed) AssertIRQ(); else ReleaseIRQ();
      prev_irq_needed = irq_needed;
    }

    // Inner bus cycle loop (GROUP_SIZE = 50)
    constexpr int GROUP_SIZE = 50;
    for (int i = 0; i < GROUP_SIZE; i++) {
      // Synchronize with Hamster PIO
      pio_sm_put(pio0, 0, 0);
      uint early_pins = pio_sm_get_blocking(pio0, 0);
      uint addr = 0xFFFF & hw->gpio_hi_in;

      cycles++;

      // Max checks
      if (UNLIKELY(max_cycles > 0 && cycles >= max_cycles)) {
        fault_reason = FAULT_MAX_CYCLES;
        fault_cycle = cycles;
        fault_addr = addr;
        fault_triggered = true;
        HaltOn();
        return;
      }
      if (UNLIKELY(max_time_us > 0 && (time_us_64() - start_time_us) >= max_time_us)) {
        fault_reason = FAULT_MAX_TIME;
        fault_cycle = cycles;
        fault_addr = addr;
        fault_triggered = true;
        HaltOn();
        return;
      }

      const bool reading = 0 != (early_pins & (1 << R_W));
      const bool is_bs   = 0 != (early_pins & (1 << BS));

      byte value = 0;
      byte kind = KIND_IDLE;

      // ── Red Page Check ($FF04..$FFEF) ──
      if (UNLIKELY(addr >= 0xFF04 && addr <= 0xFFEF)) {
        fault_reason = FAULT_RED_PAGE;
        fault_cycle = cycles;
        fault_addr = addr;
        fault_data = reading ? ram[addr] : (byte)pio_sm_get_blocking(pio0, 0);
        fault_triggered = true;
        HaltOn();
        return;
      }

      if (LIKELY(reading)) {
        // Read cycle
        if (LIKELY(addr < 0xFF00)) {
          value = ram[addr];
        } else if (addr <= 0xFF03) {
          // Turbo9Sim ACIA registers
          switch (addr & 3) {
            case 0: value = sim_last_char_tx; break;
            case 1:
              if (!term_input.Empty()) {
                value = term_input.Take();
                if (term_input.Empty()) {
                  sim_status_reg &= ~SIM_RX_BIT;
                }
              } else {
                value = 0;
              }
              break;
            case 2: value = sim_status_reg; break;
            case 3: value = sim_control_reg; break;
          }
        } else {
          // Vectors $FFF0..$FFFF
          value = ram[addr];

          // ── Zero Interrupt Vector Check ──
          // If BS=1 (Interrupt Acknowledge) and vector data is 0:
          if (UNLIKELY(is_bs && value == 0)) {
            fault_reason = FAULT_ZERO_VECTOR;
            fault_cycle = cycles;
            fault_addr = addr;
            fault_data = value;
            fault_triggered = true;
            HaltOn();
            return;
          }
        }

        // Interrupt Acknowledge: turn on LED
        if (UNLIKELY(is_bs)) {
          LedOn();
        }

        // Put data onto data bus for CPU to latch
        pio_sm_put(pio0, 0, value);
        uint late_pins = pio_sm_get_blocking(pio0, 0);

        bool is_fic = ((prev_late_pins & (1 << LIC)) != 0);

        if (addr == 0xFFFF) {
          kind = KIND_IDLE;
        } else if (is_fic) {
          kind = KIND_FIC;
        } else if ((prev_kind == KIND_FIC || prev_kind == KIND_OPCODE_CONT) &&
                   addr == (prev_addr + 1)) {
          kind = KIND_OPCODE_CONT;
        } else {
          kind = KIND_READ;
        }

        prev_late_pins = late_pins;
      } else {
        // Write cycle
        uint late_pins = pio_sm_get_blocking(pio0, 0);
        value = (byte)late_pins;

        if (LIKELY(addr < 0xFF00)) {
          ram[addr] = value;
        } else if (addr <= 0xFF03) {
          switch (addr & 3) {
            case 0:
              sim_last_char_tx = value;
              fg2bg_chars.push(value);
              break;
            case 1:
              // RX write has no effect
              break;
            case 2:
              if (value & SIM_TIMER_BIT) sim_status_reg &= ~SIM_TIMER_BIT;
              if (value & SIM_RX_BIT)    sim_status_reg &= ~SIM_RX_BIT;
              break;
            case 3:
              sim_control_reg = value;
              break;
          }
        } else {
          ram[addr] = value;
        }

        kind = (addr == 0xFFFF) ? KIND_IDLE : KIND_WRITE;
        prev_late_pins = late_pins;
      }

      // Check for RTI ($3B)
      bool is_rti = (reading && kind == KIND_FIC && value == 0x3B && addr < 0xFFF0);
      if (is_rti) {
        LedOff();
      }

      // Trace filtering
      bool trigger_met = (cycles >= trigger_cycle) &&
                         ((time_us_64() - start_time_us) >= trigger_time_us);

      if (trigger_met) {
        bool emit = false;
        switch (kind) {
          case KIND_IDLE: emit = false; break;  // Emitted only if idle tracing
          case KIND_FIC:
            emit = (trace_flags & TRACE_FLAG_X) != 0;
            break;
          case KIND_OPCODE_CONT:
            emit = (trace_flags & TRACE_FLAG_PLUS) != 0;
            break;
          case KIND_READ:
            emit = (trace_flags & TRACE_FLAG_R) != 0;
            break;
          case KIND_WRITE:
            emit = (trace_flags & TRACE_FLAG_W) != 0;
            break;
          case KIND_IRQ:
          case KIND_FIRQ:
          case KIND_NMI:
          case KIND_RTI:
            emit = (trace_flags & TRACE_FLAG_I) != 0;
            break;
          case KIND_SWI2:
            emit = (trace_flags & TRACE_FLAG_T) != 0;
            break;
        }
        if (emit) {
          TraceRecord rec{cycles, (uint16_t)addr, value, kind};
          fg2bg_trace.push(rec);
        }
        if (is_rti && (trace_flags & TRACE_FLAG_I) && !(emit && kind == KIND_RTI)) {
          TraceRecord rec_rti{cycles, (uint16_t)addr, value, KIND_RTI};
          fg2bg_trace.push(rec_rti);
        }
      }

      prev_addr = addr;
      prev_kind = kind;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// Core 0: Background Administration & USB Pipeline
// ═══════════════════════════════════════════════════════════════════
CircBuf<unsigned char, 4096> usb_rx_raw;
CircBuf<std::string*, 64> usb_rx_packets;
CobsDecoder<4096, 64> cobs_decoder(usb_rx_raw, usb_rx_packets);

void pump_usb_rx() {
  while (true) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) break;
    usb_rx_raw.Put((unsigned char)c);
  }
  cobs_decoder.Tick();
}

void send_cobs(const unsigned char* data, size_t len) {
  CobsEncodeAndTransmit(data, len, [](int ch) { putchar_raw(ch); });
}

void send_rpc_response(const pcb::RpcResponse& resp) {
  std::vector<uint8_t> payload = resp.encode();
  std::vector<uint8_t> pkt;
  pkt.reserve(payload.size() + 1);
  pkt.push_back(T_PICO_RPC);
  pkt.insert(pkt.end(), payload.begin(), payload.end());
  send_cobs(pkt.data(), pkt.size());
}

void handle_rpc_request(const std::string& pkt) {
  if (pkt.length() < 2) return;
  std::vector<uint8_t> buf(pkt.begin() + 1, pkt.end());
  pcb::RpcRequest req = pcb::RpcRequest::decode(buf);
  pcb::RpcResponse resp;
  resp.serial = req.serial;

  if (req.method == "ping") {
    resp.status = 0;
    resp.data = req.data;
    send_rpc_response(resp);
  } else if (req.method == "config") {
    trace_flags = (uint8_t)req.flags;
    trigger_cycle = (uint64_t)req.offset;
    trigger_time_us = (uint64_t)req.length;
    max_cycles = (uint64_t)req.whence;
    max_time_us = (uint64_t)req.flags; // or passed via data
    // If data payload contains binary config, unpack:
    if (req.data.size() >= 24) {
      const uint8_t* p = (const uint8_t*)req.data.data();
      trigger_cycle = *(const uint64_t*)(p + 0);
      trigger_time_us = *(const uint64_t*)(p + 8);
      max_cycles = *(const uint64_t*)(p + 16);
      if (req.data.size() >= 32) {
        max_time_us = *(const uint64_t*)(p + 24);
      }
    }
    resp.status = 0;
    send_rpc_response(resp);
  } else if (req.method == "upload") {
    size_t offset = (size_t)req.offset;
    size_t len = req.data.size();
    if (offset + len <= 65536) {
      memcpy(&ram[offset], req.data.data(), len);
      resp.status = 0;
    } else {
      resp.status = 1;
      resp.message = "offset out of range";
    }
    send_rpc_response(resp);
  } else if (req.method == "start") {
    resp.status = 0;
    send_rpc_response(resp);

    // Clear trace and char buffers before starting CPU
    fg2bg_trace.clear();
    fg2bg_chars.clear();

    // Boot CPU
    gpio_set_dir(RESET, GPIO_OUT);
    HaltOn();

    pio_sm_set_enabled(pio0, 0, false);
    pio_sm_restart(pio0, 0);
    pio_clear_instruction_memory(pio0);
    uint offset = pio_add_program(pio0, &hamster_program);
    hamster_program_init(pio0, 0, offset);

    start_time_us = time_us_64();
    alarm_pool_init_default();
    add_repeating_timer_us(16667, Timer60HzCallback, nullptr, &timer60hz_data);
    timer60hz_running = true;

    foreground_running = false;
    multicore_launch_core1(foreground_loop);

    while (!foreground_running) {
      sleep_ms(1);
    }

    // Hold RESET for ~10ms
    sleep_ms(10);
    gpio_set_dir(RESET, GPIO_IN);  // Release RESET
    sleep_ms(5);
    HaltOff();                     // Release HALT -> 6309 starts

    fw_state = STATE_RUNNING;
  } else {
    resp.status = -1;
    resp.message = "unknown method";
    send_rpc_response(resp);
  }
}

void transmit_core_dump() {
  // Send C_FAULT header: [C_FAULT, reason, cycle_8B, addr_2B, data_1B]
  uint8_t fault_pkt[13];
  fault_pkt[0] = C_FAULT;
  fault_pkt[1] = fault_reason;
  memcpy(&fault_pkt[2], (const void*)&fault_cycle, 8);
  fault_pkt[10] = (uint8_t)(fault_addr >> 8);
  fault_pkt[11] = (uint8_t)(fault_addr & 0xFF);
  fault_pkt[12] = fault_data;
  send_cobs(fault_pkt, sizeof(fault_pkt));

  // Send 64KB core dump in 64 chunks of 1024 bytes
  uint8_t dump_pkt[1026];
  dump_pkt[0] = C_CORE_DUMP;
  for (int chunk = 0; chunk < 64; chunk++) {
    dump_pkt[1] = (uint8_t)chunk;
    memcpy(&dump_pkt[2], &ram[chunk * 1024], 1024);
    send_cobs(dump_pkt, sizeof(dump_pkt));
    sleep_ms(2);  // Give USB time to flush
  }
}

void restart_to_restarted_state() {
  // 1. Assert RESET and HALT on 6309 CPU
  gpio_set_dir(RESET, GPIO_OUT);
  gpio_put(RESET, 0);
  HaltOn();
  ReleaseIRQ();

  // 2. Stop Core 1 if running
  if (foreground_running) {
    multicore_reset_core1();
    foreground_running = false;
  }

  // 3. Cancel 60Hz timer
  if (timer60hz_running) {
    cancel_repeating_timer(&timer60hz_data);
    timer60hz_running = false;
  }

  // 4. Disable Hamster PIO
  pio_sm_set_enabled(pio0, 0, false);
  pio_sm_restart(pio0, 0);
  pio_clear_instruction_memory(pio0);

  // 5. Reset internal state variables
  fault_triggered = false;
  fault_reason = 0;
  fault_cycle = 0;
  fault_addr = 0;
  fault_data = 0;
  trace_flags = 0;
  trigger_cycle = 0;
  trigger_time_us = 0;
  max_cycles = 0;
  max_time_us = 0;
  sim_status_reg = 0;
  sim_control_reg = 0;
  sim_last_char_tx = 0;

  // 6. Clear buffers
  fg2bg_trace.clear();
  fg2bg_chars.clear();
  term_input.Reset();

  // 7. Transition to STATE_WAIT_CONFIG (RESTARTED state)
  fw_state = STATE_WAIT_CONFIG;
  waiting_for_tether_packet = true;
  last_beacon_us = time_us_64();

  // Send initial C_RESTARTED beacon
  unsigned char pkt[] = {C_RESTARTED, 1, 0};
  send_cobs(pkt, sizeof(pkt));
  LedOn();
}

void reflash_now_please() {
  // 1. Assert RESET and HALT on 6309 CPU
  gpio_set_dir(RESET, GPIO_OUT);
  gpio_put(RESET, 0);
  HaltOn();
  ReleaseIRQ();

  // 2. Stop Core 1 if running
  if (foreground_running) {
    multicore_reset_core1();
    foreground_running = false;
  }

  // 3. Cancel 60Hz timer
  if (timer60hz_running) {
    cancel_repeating_timer(&timer60hz_data);
    timer60hz_running = false;
  }

  // 4. Disable Hamster PIO
  pio_sm_set_enabled(pio0, 0, false);
  pio_sm_restart(pio0, 0);
  pio_clear_instruction_memory(pio0);

  sleep_ms(20);
  reset_usb_boot(0, 0);
}

int main() {
  set_sys_clock_khz(250000, true);
  stdio_usb_init();
  InitializePins();

  restart_to_restarted_state();

  while (true) {
    pump_usb_rx();

    // ── STATE: WAIT CONFIG ──
    if (fw_state == STATE_WAIT_CONFIG) {
      uint64_t now = time_us_64();

      // Flash the LED at 2Hz with a 50% duty cycle when in RESTARTED mode,
      // waiting for any packet from the Tether.
      // 2Hz period = 500,000 us; 50% duty cycle = 250,000 us ON, 250,000 us OFF.
      if (waiting_for_tether_packet) {
        if ((now % 500000) < 250000) {
          LedOn();
        } else {
          LedOff();
        }

        if (now - last_beacon_us >= 500000) {  // Every 0.5s
          unsigned char pkt[] = {C_RESTARTED, 1, 0};  // Version 1
          send_cobs(pkt, sizeof(pkt));
          last_beacon_us = now;
        }
      } else {
        LedOff();
      }

      while (usb_rx_packets.NumBuffered() > 0) {
        std::string* pkt = usb_rx_packets.Take();
        if (pkt && pkt->length() > 0) {
          byte cmd = (byte)(*pkt)[0];
          if (cmd == T_REFLASH_NOW_PLEASE || cmd == 175 || cmd == 176) {
            delete pkt;
            reflash_now_please();
            break;
          }
          if (cmd == T_RESTART_NOW_PLEASE) {
            delete pkt;
            restart_to_restarted_state();
            break;
          }
          waiting_for_tether_packet = false;
          LedOff();
          if (cmd == T_PICO_RPC) {
            handle_rpc_request(*pkt);
          }
        }
        delete pkt;
      }
    }

    // ── STATE: RUNNING ──
    else if (fw_state == STATE_RUNNING) {
      // 1. Drain console characters
      byte ch;
      while (fg2bg_chars.pop(ch)) {
        unsigned char pkt[] = {C_PUTCHAR, ch};
        send_cobs(pkt, sizeof(pkt));
      }

      // 2. Drain trace cycles (bundle up to 20 records per packet)
      constexpr size_t MAX_BATCH = 20;
      TraceRecord batch[MAX_BATCH];
      size_t count = 0;
      while (count < MAX_BATCH && fg2bg_trace.pop(batch[count])) {
        count++;
      }
      if (count > 0) {
        uint8_t pkt[1 + 1 + MAX_BATCH * 12];
        pkt[0] = C_TRACE_CYCLES;
        pkt[1] = (uint8_t)count;
        for (size_t i = 0; i < count; i++) {
          size_t off = 2 + i * 12;
          memcpy(&pkt[off + 0], &batch[i].cycle, 8);
          pkt[off + 8] = (uint8_t)(batch[i].addr >> 8);
          pkt[off + 9] = (uint8_t)(batch[i].addr & 0xFF);
          pkt[off + 10] = batch[i].data;
          pkt[off + 11] = batch[i].kind;
        }
        send_cobs(pkt, 2 + count * 12);
      }

      // 3. Process USB packets from Tether
      while (usb_rx_packets.NumBuffered() > 0) {
        std::string* pkt = usb_rx_packets.Take();
        if (pkt && pkt->length() > 0) {
          byte cmd = (byte)(*pkt)[0];
          if (cmd == T_REFLASH_NOW_PLEASE || cmd == 175 || cmd == 176) {
            delete pkt;
            reflash_now_please();
            break;
          }
          if (cmd == T_RESTART_NOW_PLEASE) {
            delete pkt;
            restart_to_restarted_state();
            break;
          } else if (cmd == T_CONSOLE_LINE || cmd == 178 || cmd == 179) {
            // Push line into term_input
            for (size_t i = 1; i < pkt->length(); i++) {
              if (!term_input.Full()) {
                term_input.Put((byte)(*pkt)[i]);
              }
            }
          } else if (cmd == T_PICO_RPC) {
            handle_rpc_request(*pkt);
          }
        }
        delete pkt;
      }

      // 4. Check for Fault
      if (UNLIKELY(fault_triggered)) {
        fw_state = STATE_FAULT;
      }
    }

    // ── STATE: FAULT ──
    else if (fw_state == STATE_FAULT) {
      transmit_core_dump();
      fw_state = STATE_HALTED;
    }

    // ── STATE: HALTED ──
    else if (fw_state == STATE_HALTED) {
      sleep_ms(100);
      while (usb_rx_packets.NumBuffered() > 0) {
        std::string* pkt = usb_rx_packets.Take();
        if (pkt && pkt->length() > 0) {
          byte cmd = (byte)(*pkt)[0];
          if (cmd == T_REFLASH_NOW_PLEASE || cmd == 175 || cmd == 176) {
            delete pkt;
            reflash_now_please();
            break;
          }
          if (cmd == T_RESTART_NOW_PLEASE) {
            delete pkt;
            restart_to_restarted_state();
            break;
          } else if (cmd == T_PICO_RPC) {
            handle_rpc_request(*pkt);
          }
        }
        delete pkt;
      }
    }
  }

  return 0;
}
