#pragma once
// rp2350b_diag.h -- Non-destructive 48-GPIO electrical & bus diagnostic for RP2350B boards
// (TFR/911H and Centipede 32z, both populated/plugged-in and unpopulated/fresh-out-of-box).
//
// Provides the `rp2350b` Tcl command:
//   rp2350b ?diag?       human-readable 48-pin summary, clocks, shorts & board verdict
//   rp2350b raw          compact machine-parsable report for scripts/diagnose_rp2350b.sh
//   rp2350b pin <0..47>  detailed single-pin pull + ~1-cycle @ 150MHz impulse report
//   rp2350b clocks       measure toggling frequencies & GPIO 21/22 quadrature phase
//   rp2350b shorts       scan all HI_Z GPIOs for pin-to-pin solder bridges
//
// All timing-critical register probes (`SampleBurst48Sram`, `CountEdgesSram`,
// `ShortOutputImpulseSram`) are leaf `IN_RAM` routines making zero flash calls, so
// `scripts/check-core1-sram.sh -s` stays 100% clean.

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/io_bank0.h>
#include <hardware/structs/pads_bank0.h>
#include <hardware/structs/sio.h>
#include <hardware/structs/timer.h>
#include <hardware/sync.h>
#include <pico/unique_id.h>

namespace rp2350b_diag {

constexpr uint kNumGpios = 48;
constexpr uint kBurstSamples = 1024;

static uint32_t s_burst_lo[kBurstSamples];
static uint16_t s_burst_hi[kBurstSamples];

enum PinClass : uint8_t {
  PC_HI_Z = 0,      // Floating high-Z: pulls move it AND pad capacitance holds both 0 and 1
  PC_WK_PU = 1,     // Weak external pull-up (~40k-200k, e.g. ESP32 internal pull-up)
  PC_WK_PD = 2,     // Weak external pull-down / leakage (~40k-200k)
  PC_PULLUP = 3,    // External pull-up resistor (~1k-33k): beats 50k PD, yields to short impulse
  PC_PULLDN = 4,    // External pull-down / LED load: beats 50k PU, yields to short impulse
  PC_DRV_HIGH = 5,  // Active push-pull HIGH driver (<100 ohm: Schmitt, 63x09E, SAM, ESP32 TX)
  PC_DRV_LOW = 6,   // Active push-pull LOW driver (<100 ohm or hard GND tie)
  PC_TOGGLE = 7,    // Actively switching bus / data / address signal
  PC_CLOCK = 8,     // Periodic clock (>=100 kHz, ~25..75% duty)
};

inline const char* PinClassName(PinClass c) {
  switch (c) {
    case PC_HI_Z:     return "HI_Z";
    case PC_WK_PU:    return "WK_PU";
    case PC_WK_PD:    return "WK_PD";
    case PC_PULLUP:   return "PULLUP";
    case PC_PULLDN:   return "PULLDN";
    case PC_DRV_HIGH: return "DRV_HIGH";
    case PC_DRV_LOW:  return "DRV_LOW";
    case PC_TOGGLE:   return "TOGGLE";
    case PC_CLOCK:    return "CLOCK";
    default:          return "?";
  }
}

struct PinDiag {
  PinClass cls;
  uint8_t pd;       // level under internal ~50k pull-down
  uint8_t pu;       // level under internal ~50k pull-up
  uint8_t lf;       // fast read (~8ns) right after ~6.7-8ns LOW output pulse
  uint8_t lh;       // settled read (+20us) after LOW output pulse (pulls off)
  uint8_t hf;       // fast read (~8ns) right after ~6.7-8ns HIGH output pulse
  uint8_t hh;       // settled read (+20us) after HIGH output pulse (pulls off)
  uint8_t duty_pct; // 0..100
  uint16_t burst_edges;
  uint32_t freq_hz;
};

struct ShortPair {
  uint8_t a;
  uint8_t b;
};

struct CpuSocketTest {
  bool ran;
  uint16_t drv_abus_mask;  // bitmask of A0..A15 actively driven during RESET
  uint16_t rst_abus_val;   // A15..A0 value sampled during RESET (expected 0xFFFF)
  uint8_t ctrl_drv_mask;   // bit0=LIC(26), bit1=AVMA(27), bit2=BS(28), bit3=RW(31) actively driven
  uint8_t ctrl_rst_val;    // values of [RW,BS,AVMA,LIC] during RESET (expected 0b1000 = 0x8)
  bool saw_fffe;           // saw BS=1 & A=0xFFFE after releasing RESET#
  bool saw_ffff;           // saw BS=1 & A=0xFFFF after releasing RESET#
};

struct FullReport {
  uint32_t sys_mhz;
  uint32_t burst_dt_us;
  char pico_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  PinDiag pins[kNumGpios];
  // Quadrature analysis on (GPIO 21, GPIO 22):
  uint16_t eq_state_counts[4];  // [0]=00, [1]=01, [2]=10, [3]=11
  uint16_t eq_quad_fwd;         // 00->01->11->10->00 steps
  uint16_t eq_quad_rev;         // 00->10->11->01->00 steps
  uint16_t eq_quad_diag;        // 00<->11 or 01<->10 simultaneous jumps
  // Pin-to-pin bridges among HI_Z pins:
  ShortPair shorts[32];
  uint num_shorts;
  // Conditional TFR/911H 63x09E socket verification:
  CpuSocketTest cpu;
};

// ── Leaf IN_RAM routines (strictly zero flash calls!) ──

// Captures `count` back-to-back 48-GPIO snapshots from SIO at ~30-40 MSa/s.
static void __attribute__((noinline)) IN_RAM SampleBurst48Sram(
    uint32_t* lo_buf, uint16_t* hi_buf, uint32_t count, uint32_t* out_dt_us) {
  uint32_t primask;
  __asm volatile("mrs %0, PRIMASK\n\tcpsid i" : "=r"(primask) :: "memory");
  uint32_t t0 = timer_hw->timerawl;
  for (uint32_t i = 0; i < count; i++) {
    lo_buf[i] = sio_hw->gpio_in;
    hi_buf[i] = (uint16_t)(sio_hw->gpio_hi_in & 0xFFFFu);
  }
  uint32_t t1 = timer_hw->timerawl;
  __asm volatile("msr PRIMASK, %0" :: "r"(primask) : "memory");
  *out_dt_us = (t1 - t0);
}

// Counts transitions on a single GPIO over `window_us` at >50 MSa/s.
static uint32_t __attribute__((noinline)) IN_RAM CountEdgesSram(
    const volatile uint32_t* in_reg, uint32_t mask, uint32_t window_us,
    uint32_t* out_dt_us, uint32_t* out_duty_pct) {
  uint32_t primask;
  __asm volatile("mrs %0, PRIMASK\n\tcpsid i" : "=r"(primask) :: "memory");
  uint32_t edges = 0;
  uint32_t high_cnt = 0;
  uint32_t total_cnt = 0;
  uint32_t prev = (*in_reg) & mask;
  uint32_t t0 = timer_hw->timerawl;
  uint32_t dt = 0;
  do {
    // Unroll 16 samples between timer checks for high Nyquist ceiling (>25 MHz).
    for (uint k = 0; k < 16; k++) {
      uint32_t cur = (*in_reg) & mask;
      if (cur != prev) {
        edges++;
        prev = cur;
      }
      if (cur) high_cnt++;
    }
    total_cnt += 16;
    dt = timer_hw->timerawl - t0;
  } while (dt < window_us);
  __asm volatile("msr PRIMASK, %0" :: "r"(primask) : "memory");
  *out_dt_us = dt ? dt : 1;
  *out_duty_pct = total_cnt ? ((high_cnt * 100u + total_cnt / 2u) / total_cnt) : 0;
  return edges;
}

// Drives a single GPIO to a pre-staged level for ~1 cycle at 150 MHz (~6.7-8 ns:
// 2 cycles at 250 MHz, 1 cycle at <=160 MHz), immediately returns OE to Hi-Z,
// waits 2 cycles for the RP2350B 2-FF input synchronizer, and returns the sampled bit.
static uint32_t __attribute__((noinline)) IN_RAM ShortOutputImpulseSram(
    volatile uint32_t* out_reg, volatile uint32_t* oe_set, volatile uint32_t* oe_clr,
    const volatile uint32_t* in_reg, uint32_t mask, uint32_t two_cycles) {
  *out_reg = mask;  // Stage output latch while OE is still 0.
  uint32_t sampled = 0;
  uint32_t primask;
  __asm volatile("mrs %0, PRIMASK\n\tcpsid i" : "=r"(primask) :: "memory");
  if (two_cycles) {
    __asm volatile(
        "str %[mask], [%[oe_set]]\n\t"  // t=0ns: enable pad driver
        "nop\n\t"                       // +4ns @ 250MHz (~1 cycle @ 150MHz = 6.7ns)
        "str %[mask], [%[oe_clr]]\n\t"  // t=8ns: tri-state pad driver
        "nop\n\tnop\n\tnop\n\t"         // allow pad slew + Schmitt + 2-FF input sync (~20ns)
        "nop\n\tnop\n\tnop\n\t"
        "ldr %[res],  [%[in_reg]]\n\t"  // read synchronized input right after release (~24ns)
        : [res] "=r"(sampled)
        : [mask] "r"(mask), [oe_set] "r"(oe_set), [oe_clr] "r"(oe_clr), [in_reg] "r"(in_reg)
        : "memory");
  } else {
    __asm volatile(
        "str %[mask], [%[oe_set]]\n\t"
        "str %[mask], [%[oe_clr]]\n\t"
        "nop\n\tnop\n\tnop\n\tnop\n\t"
        "ldr %[res],  [%[in_reg]]\n\t"
        : [res] "=r"(sampled)
        : [mask] "r"(mask), [oe_set] "r"(oe_set), [oe_clr] "r"(oe_clr), [in_reg] "r"(in_reg)
        : "memory");
  }
  __asm volatile("msr PRIMASK, %0" :: "r"(primask) : "memory");
  return (sampled & mask) ? 1u : 0u;
}

// ── Core-0 flash helpers ──

inline uint32_t ReadPinBit(uint p) {
  if (p < 32) return (sio_hw->gpio_in >> p) & 1u;
  return (sio_hw->gpio_hi_in >> (p - 32)) & 1u;
}

inline void SetPadPulls(uint p, bool pue, bool pde) {
  uint32_t v = PADS_BANK0_GPIO0_IE_BITS |
               (1u << PADS_BANK0_GPIO0_DRIVE_LSB) |  // 4mA (~90 ohm)
               PADS_BANK0_GPIO0_SCHMITT_BITS;
  if (pue) v |= PADS_BANK0_GPIO0_PUE_BITS;
  if (pde) v |= PADS_BANK0_GPIO0_PDE_BITS;
  pads_bank0_hw->io[p] = v;
}

inline void ConfigureSioInputNoPull(uint p) {
  if (p < 32) sio_hw->gpio_oe_clr = (1u << p);
  else        sio_hw->gpio_hi_oe_clr = (1u << (p - 32));
  io_bank0_hw->io[p].ctrl = GPIO_FUNC_SIO;  // clears pad overrides too
  SetPadPulls(p, false, false);
}

inline uint8_t ImpulseTestPin(uint p, bool drive_high, uint32_t sys_mhz, uint8_t* out_hold) {
  uint32_t mask = (p < 32) ? (1u << p) : (1u << (p - 32));
  volatile uint32_t* out_reg = (p < 32)
      ? (drive_high ? &sio_hw->gpio_set : &sio_hw->gpio_clr)
      : (drive_high ? &sio_hw->gpio_hi_set : &sio_hw->gpio_hi_clr);
  volatile uint32_t* oe_set = (p < 32) ? &sio_hw->gpio_oe_set : &sio_hw->gpio_hi_oe_set;
  volatile uint32_t* oe_clr = (p < 32) ? &sio_hw->gpio_oe_clr : &sio_hw->gpio_hi_oe_clr;
  const volatile uint32_t* in_reg = (p < 32) ? &sio_hw->gpio_in : &sio_hw->gpio_hi_in;

  uint8_t fast = (uint8_t)ShortOutputImpulseSram(
      out_reg, oe_set, oe_clr, in_reg, mask, (sys_mhz > 160) ? 1u : 0u);
  busy_wait_us_32(20);
  if (out_hold) *out_hold = (uint8_t)ReadPinBit(p);
  return fast;
}

inline void ProbeStaticAndImpulse(uint p, uint32_t sys_mhz, PinDiag* d) {
  ConfigureSioInputNoPull(p);

  // 1. Internal pull-down (~50k):
  SetPadPulls(p, false, true);
  busy_wait_us_32(25);
  uint8_t pd0 = (uint8_t)ReadPinBit(p);
  busy_wait_us_32(5);
  uint8_t pd1 = (uint8_t)ReadPinBit(p);

  // 2. Internal pull-up (~50k) -- also leaves HI_Z pin pre-charged to 1:
  SetPadPulls(p, true, false);
  busy_wait_us_32(25);
  uint8_t pu0 = (uint8_t)ReadPinBit(p);
  busy_wait_us_32(5);
  uint8_t pu1 = (uint8_t)ReadPinBit(p);

  if (pd0 != pd1 || pu0 != pu1) {
    // Caught a slow transition during pull test!
    SetPadPulls(p, false, false);
    d->pd = pd1; d->pu = pu1;
    d->lf = 0; d->lh = 0; d->hf = 1; d->hh = 1;
    d->cls = PC_TOGGLE;
    return;
  }
  d->pd = pd1;
  d->pu = pu1;

  // 3. Disable internal pulls; pad is currently at `pu` (1 for HI_Z / PULLUP / DRV_HIGH).
  SetPadPulls(p, false, false);
  busy_wait_us_32(5);

  // Fire ~1-cycle @ 150MHz LOW impulse first (tests whether HIGH state is stiff vs resistive vs capacitive):
  d->lf = ImpulseTestPin(p, /*drive_high=*/false, sys_mhz, &d->lh);
  // Then fire ~1-cycle @ 150MHz HIGH impulse (tests whether LOW state is stiff vs resistive vs capacitive):
  d->hf = ImpulseTestPin(p, /*drive_high=*/true, sys_mhz, &d->hh);

  // 4. Classify impedance & external driver:
  if (d->pd == 1 && d->pu == 1) {
    if (d->lf == 1) {
      d->cls = PC_DRV_HIGH;  // Snapped right back to 1 within ~8ns
    } else {
      d->cls = PC_PULLUP;    // Discharged to 0 on short pulse, recharged to 1 via pull-up
    }
    d->duty_pct = 100;
  } else if (d->pd == 0 && d->pu == 0) {
    if (d->hf == 0) {
      d->cls = PC_DRV_LOW;   // Snapped right back to 0 within ~8ns
    } else {
      d->cls = PC_PULLDN;    // Charged to 1 on short pulse, bled to 0 via LED / pull-down
    }
    d->duty_pct = 0;
  } else {
    // Internal 50k pull-up & pull-down moved the pin (pd=0, pu=1):
    if (d->lf == 0 && d->lh == 1) {
      d->cls = PC_WK_PU;     // Weak ~45k-200k external pull-up (e.g. ESP32 IO33)
      d->duty_pct = 100;
    } else if (d->hf == 1 && d->hh == 0) {
      d->cls = PC_WK_PD;     // Weak ~45k-200k external pull-down / leakage
      d->duty_pct = 0;
    } else {
      d->cls = PC_HI_Z;      // True floating high-Z (capacitive bus-hold retained both 0 and 1)
      d->duty_pct = 50;
    }
  }
}

inline void RunFullProbe(FullReport* rep, bool run_shorts = true, bool run_cpu_test = true) {
  memset(rep, 0, sizeof(*rep));
  rep->sys_mhz = clock_get_hz(clk_sys) / 1000000u;
  if (rep->sys_mhz == 0) rep->sys_mhz = 150;
  pico_get_unique_board_id_string(rep->pico_id, sizeof(rep->pico_id));

  // Save PIO0 SM0 & all 48 GPIO states:
  bool sm0_was_enabled = (pio0->ctrl & 1u) != 0;
  if (sm0_was_enabled) {
#if defined(_BOARDS_CENTIPEDE32Z_H)
    // Detach CoCo VDG spoonfeeder channel before pausing PIO0 SM0 so Core 0's
    // USB REPL / PicoRPC never waits on Core 1 BG2FG_POKE after probing.
    tcl_io::active_io &= ~tcl_io::IO_COCO2;
#endif
    pio_sm_set_enabled(pio0, 0, false);
  }
  uint32_t saved_io_ctrl[kNumGpios];
  uint32_t saved_pad[kNumGpios];
  for (uint p = 0; p < kNumGpios; p++) {
    saved_io_ctrl[p] = io_bank0_hw->io[p].ctrl;
    saved_pad[p] = pads_bank0_hw->io[p];
  }
  uint32_t saved_oe_lo = sio_hw->gpio_oe;
  uint32_t saved_oe_hi = sio_hw->gpio_hi_oe & 0xFFFFu;
  uint32_t saved_out_lo = sio_hw->gpio_out;
  uint32_t saved_out_hi = sio_hw->gpio_hi_out & 0xFFFFu;

  // Put all 48 GPIOs into clean SIO input mode (no pulls) for unbiased passive observation:
  sio_hw->gpio_oe_clr = 0xFFFFFFFFu;
  sio_hw->gpio_hi_oe_clr = 0x0000FFFFu;
  for (uint p = 0; p < kNumGpios; p++) {
    ConfigureSioInputNoPull(p);
  }
  busy_wait_us_32(30);

  // Pass 1: Simultaneous 48-pin high-speed burst capture (~30-40 MSa/s)
  SampleBurst48Sram(s_burst_lo, s_burst_hi, kBurstSamples, &rep->burst_dt_us);

  for (uint p = 0; p < kNumGpios; p++) {
    uint16_t edges = 0;
    uint16_t ones = 0;
    if (p < 32) {
      uint32_t m = (1u << p);
      uint32_t prev = s_burst_lo[0] & m;
      if (prev) ones++;
      for (uint i = 1; i < kBurstSamples; i++) {
        uint32_t cur = s_burst_lo[i] & m;
        if (cur) ones++;
        if (cur != prev) { edges++; prev = cur; }
      }
    } else {
      uint16_t m = (uint16_t)(1u << (p - 32));
      uint16_t prev = s_burst_hi[0] & m;
      if (prev) ones++;
      for (uint i = 1; i < kBurstSamples; i++) {
        uint16_t cur = s_burst_hi[i] & m;
        if (cur) ones++;
        if (cur != prev) { edges++; prev = cur; }
      }
    }
    rep->pins[p].burst_edges = edges;
    rep->pins[p].duty_pct = (uint8_t)((ones * 100u + kBurstSamples / 2u) / kBurstSamples);
  }

  // Quadrature state analysis on (GPIO 21, GPIO 22):
  uint prev_eq = ((s_burst_lo[0] >> 21) & 1u) | (((s_burst_lo[0] >> 22) & 1u) << 1);
  rep->eq_state_counts[prev_eq]++;
  for (uint i = 1; i < kBurstSamples; i++) {
    uint cur_eq = ((s_burst_lo[i] >> 21) & 1u) | (((s_burst_lo[i] >> 22) & 1u) << 1);
    rep->eq_state_counts[cur_eq]++;
    if (cur_eq != prev_eq) {
      // Gray-code forward: 0(00)->1(01)->3(11)->2(10)->0(00)
      if ((prev_eq == 0 && cur_eq == 1) || (prev_eq == 1 && cur_eq == 3) ||
          (prev_eq == 3 && cur_eq == 2) || (prev_eq == 2 && cur_eq == 0)) {
        rep->eq_quad_fwd++;
      } else if ((prev_eq == 0 && cur_eq == 2) || (prev_eq == 2 && cur_eq == 3) ||
                 (prev_eq == 3 && cur_eq == 1) || (prev_eq == 1 && cur_eq == 0)) {
        rep->eq_quad_rev++;
      } else {
        rep->eq_quad_diag++;
      }
      prev_eq = cur_eq;
    }
  }

  // Pass 2: Precision frequency meter on toggling pins + candidate clock pins (21, 22, 29, 30)
  for (uint p = 0; p < kNumGpios; p++) {
    bool is_clk_cand = (p == 21 || p == 22 || p == 29 || p == 30);
    if (rep->pins[p].burst_edges >= 2 || is_clk_cand) {
      uint32_t win_us = (p == 21 || p == 22) ? 2000u : 400u;
      uint32_t mask = (p < 32) ? (1u << p) : (1u << (p - 32));
      const volatile uint32_t* in_reg = (p < 32) ? &sio_hw->gpio_in : &sio_hw->gpio_hi_in;
      uint32_t dt_us = 1, duty = 0;
      uint32_t edges = CountEdgesSram(in_reg, mask, win_us, &dt_us, &duty);
      if (edges >= 4) {
        // 2 edges = 1 full cycle: freq_hz = (edges * 1,000,000) / (2 * dt_us)
        rep->pins[p].freq_hz = (uint32_t)(((uint64_t)edges * 500000ULL + dt_us / 2u) / dt_us);
        rep->pins[p].duty_pct = (uint8_t)duty;
        if (rep->pins[p].freq_hz >= 100000u && duty >= 25u && duty <= 75u &&
            (p == 21 || p == 22 || p == 29 || p == 30)) {
          rep->pins[p].cls = PC_CLOCK;
        } else {
          rep->pins[p].cls = PC_TOGGLE;
        }
        rep->pins[p].pd = 1;
        rep->pins[p].pu = 1;
        rep->pins[p].lf = 1;
        rep->pins[p].lh = 1;
        rep->pins[p].hf = 0;
        rep->pins[p].hh = 0;
      }
    }
  }

  // Pass 3: Pull-up / pull-down + ~1-cycle @ 150MHz output impulse on non-toggling pins
  for (uint p = 0; p < kNumGpios; p++) {
    if (rep->pins[p].freq_hz == 0) {
      ProbeStaticAndImpulse(p, rep->sys_mhz, &rep->pins[p]);
    }
  }

  // Pass 4: Non-destructive pin-to-pin bridge scan across verified HI_Z pins
  if (run_shorts) {
    uint64_t hiz_mask = 0;
    for (uint p = 0; p < kNumGpios; p++) {
      if (rep->pins[p].cls == PC_HI_Z) {
        hiz_mask |= (1ULL << p);
        SetPadPulls(p, false, true);  // weak 50k pull-down on all HI_Z pins
      }
    }
    busy_wait_us_32(30);
    // Exclude any pin that failed to settle low under pull-down:
    uint64_t snap0 = ((uint64_t)(sio_hw->gpio_hi_in & 0xFFFFu) << 32) | sio_hw->gpio_in;
    hiz_mask &= ~snap0;

    for (uint p = 0; p < kNumGpios; p++) {
      if (!(hiz_mask & (1ULL << p))) continue;
      // Drive verified HI_Z pin `p` HIGH at 2mA against 50k pull-downs on all other HI_Z pins (<66uA if bridged):
      pads_bank0_hw->io[p] = PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_SCHMITT_BITS;  // 2mA, no pulls
      if (p < 32) {
        sio_hw->gpio_set = (1u << p);
        sio_hw->gpio_oe_set = (1u << p);
      } else {
        sio_hw->gpio_hi_set = (1u << (p - 32));
        sio_hw->gpio_hi_oe_set = (1u << (p - 32));
      }
      busy_wait_us_32(10);
      uint64_t snap = ((uint64_t)(sio_hw->gpio_hi_in & 0xFFFFu) << 32) | sio_hw->gpio_in;
      if (p < 32) {
        sio_hw->gpio_oe_clr = (1u << p);
        sio_hw->gpio_clr = (1u << p);
      } else {
        sio_hw->gpio_hi_oe_clr = (1u << (p - 32));
        sio_hw->gpio_hi_clr = (1u << (p - 32));
      }
      SetPadPulls(p, false, true);
      busy_wait_us_32(10);

      uint64_t hits = snap & hiz_mask & ~(1ULL << p);
      for (uint q = p + 1; q < kNumGpios; q++) {
        if ((hits & (1ULL << q)) && rep->num_shorts < 32) {
          rep->shorts[rep->num_shorts++] = {(uint8_t)p, (uint8_t)q};
        }
      }
    }
    for (uint p = 0; p < kNumGpios; p++) {
      if (hiz_mask & (1ULL << p)) SetPadPulls(p, false, false);
    }
  }

  // Pass 5: Conditional TFR/911H 63x09E / 6809E socket verification
  // Safe ONLY if GPIO 21/22 are NOT clocks and NOT stiffly driven (not Centipede Schmitt triggers),
  // GPIO 29 (E) and GPIO 30 (Q) are HI_Z, and GPIO 20 (RESET#) is not stiffly driven high.
  bool eq21_22_driven = (rep->pins[21].cls == PC_CLOCK || rep->pins[21].cls == PC_TOGGLE ||
                         rep->pins[21].cls == PC_DRV_HIGH || rep->pins[21].cls == PC_DRV_LOW ||
                         rep->pins[22].cls == PC_CLOCK || rep->pins[22].cls == PC_TOGGLE ||
                         rep->pins[22].cls == PC_DRV_HIGH || rep->pins[22].cls == PC_DRV_LOW);
  auto is_high_z_like = [](PinClass c) {
    return c == PC_HI_Z || c == PC_WK_PU || c == PC_WK_PD;
  };
  bool eq29_30_hiz = is_high_z_like(rep->pins[29].cls) && is_high_z_like(rep->pins[30].cls);
  bool rst20_safe = (rep->pins[20].cls != PC_DRV_HIGH && rep->pins[20].cls != PC_CLOCK &&
                     rep->pins[20].cls != PC_TOGGLE);
  bool halt24_safe = (rep->pins[24].cls != PC_DRV_LOW && rep->pins[24].cls != PC_CLOCK &&
                      rep->pins[24].cls != PC_TOGGLE);

  if (run_cpu_test && !eq21_22_driven && eq29_30_hiz && rst20_safe && halt24_safe) {
    rep->cpu.ran = true;
    // Pull up D0..D7 ($FF) and NMI#/IRQ#/FIRQ#/HALT# (21..24):
    for (uint p = 0; p < 8; p++) SetPadPulls(p, true, false);
    for (uint p = 21; p <= 24; p++) SetPadPulls(p, true, false);
    // Assert RESET# (GPIO 20 = 0, 4mA) and configure E (29) & Q (30) outputs = 0:
    sio_hw->gpio_clr = (1u << 20) | (1u << 29) | (1u << 30);
    sio_hw->gpio_oe_set = (1u << 20) | (1u << 29) | (1u << 30);
    busy_wait_us_32(10);

    auto delay_250ns = [&]() {
      for (uint32_t d = 0; d < rep->sys_mhz / 4u; d++) {
        __asm volatile("nop");
      }
    };

    auto step_eq_cycle = [&](uint16_t* out_p3_abus, uint32_t* out_p3_lo) {
      // Phase 1: E=0, Q=0 (~1 MHz E/Q clock: valid for all 6809E & 63x09E grades)
      sio_hw->gpio_clr = (1u << 29) | (1u << 30);
      delay_250ns();
      // Phase 2: E=0, Q=1
      sio_hw->gpio_set = (1u << 30);
      delay_250ns();
      // Phase 3: E=1, Q=1
      sio_hw->gpio_set = (1u << 29);
      delay_250ns();
      if (out_p3_abus) *out_p3_abus = (uint16_t)(sio_hw->gpio_hi_in & 0xFFFFu);
      if (out_p3_lo)   *out_p3_lo   = sio_hw->gpio_in;
      // Phase 4: E=1, Q=0
      sio_hw->gpio_clr = (1u << 30);
      delay_250ns();
      // Back to E=0, Q=0
      sio_hw->gpio_clr = (1u << 29);
    };

    for (int cyc = 0; cyc < 128; cyc++) {
      step_eq_cycle(nullptr, nullptr);
    }
    busy_wait_us_32(2);

    // Probe A0..A15 (GPIO 32..47) and LIC(26), AVMA(27), BS(28), RW(31) while RESET# is held LOW
    // and HALT# is pulled HIGH (BA=0, so a socketed CPU actively drives A0..A15 and status pins):
    rep->cpu.rst_abus_val = (uint16_t)(sio_hw->gpio_hi_in & 0xFFFFu);
    uint32_t rst_lo = sio_hw->gpio_in;
    rep->cpu.ctrl_rst_val = (uint8_t)(((rst_lo >> 26) & 7u) | (((rst_lo >> 31) & 1u) << 3));

    for (uint bit = 0; bit < 16; bit++) {
      PinDiag td = {};
      ProbeStaticAndImpulse(32u + bit, rep->sys_mhz, &td);
      if (td.cls == PC_DRV_HIGH || td.cls == PC_DRV_LOW) {
        rep->cpu.drv_abus_mask |= (uint16_t)(1u << bit);
      }
    }
    const uint ctrl_pins[4] = {26, 27, 28, 31};
    for (uint k = 0; k < 4; k++) {
      PinDiag td = {};
      ProbeStaticAndImpulse(ctrl_pins[k], rep->sys_mhz, &td);
      if (td.cls == PC_DRV_HIGH || td.cls == PC_DRV_LOW) {
        rep->cpu.ctrl_drv_mask |= (uint8_t)(1u << k);
      }
    }

    // Only run the RESET#-release vector-fetch check if a CPU is actually driving the bus.
    // Follows background.h's exact sequence (~10ms RESET#+HALT# low, ~6ms RESET# released
    // while HALT# held low, then release HALT# and watch for BS=1 fetch at $FFFE -> $FFFF):
    if (rep->cpu.drv_abus_mask != 0 || rep->cpu.ctrl_drv_mask != 0) {
      sio_hw->gpio_clr = (1u << 20) | (1u << 24);
      sio_hw->gpio_oe_set = (1u << 20) | (1u << 24);
      for (int cyc = 0; cyc < 10000; cyc++) {
        step_eq_cycle(nullptr, nullptr);
      }
      // Release RESET# (open-drain + 50k pull-up) while still HALTed for ~6ms:
      sio_hw->gpio_oe_clr = (1u << 20);
      SetPadPulls(20, true, false);
      for (int cyc = 0; cyc < 6000; cyc++) {
        step_eq_cycle(nullptr, nullptr);
      }
      // Release HALT# (open-drain + 50k pull-up) and watch up to ~10ms for $FFFE -> $FFFF:
      sio_hw->gpio_oe_clr = (1u << 24);
      SetPadPulls(24, true, false);
      for (int cyc = 0; cyc < 10000; cyc++) {
        uint16_t ab = 0;
        uint32_t lo = 0;
        step_eq_cycle(&ab, &lo);
        bool bs = ((lo >> 28) & 1u) != 0;
        bool rw = ((lo >> 31) & 1u) != 0;
        if (bs && rw && ab == 0xFFFEu) rep->cpu.saw_fffe = true;
        if (bs && rw && ab == 0xFFFFu && rep->cpu.saw_fffe) {
          rep->cpu.saw_ffff = true;
          break;
        }
      }
    }

    // Leave CPU cleanly reset with E=0, Q=0 before restoring saved pin state:
    sio_hw->gpio_clr = (1u << 20) | (1u << 29) | (1u << 30);
    sio_hw->gpio_oe_set = (1u << 20);
    for (int cyc = 0; cyc < 4; cyc++) step_eq_cycle(nullptr, nullptr);
    sio_hw->gpio_oe_clr = (1u << 20) | (1u << 29) | (1u << 30);
  }

  // Restore all saved GPIO & PIO0 states:
  sio_hw->gpio_oe_clr = 0xFFFFFFFFu;
  sio_hw->gpio_hi_oe_clr = 0x0000FFFFu;
  sio_hw->gpio_out = saved_out_lo;
  sio_hw->gpio_hi_set = saved_out_hi;
  sio_hw->gpio_hi_clr = (~saved_out_hi) & 0xFFFFu;
  for (uint p = 0; p < kNumGpios; p++) {
    pads_bank0_hw->io[p] = saved_pad[p];
    io_bank0_hw->io[p].ctrl = saved_io_ctrl[p];
  }
  sio_hw->gpio_oe = saved_oe_lo;
  sio_hw->gpio_hi_oe_set = saved_oe_hi;
#if defined(_BOARDS_CENTIPEDE32Z_H)
  if (sm0_was_enabled && !::startup_e_clock_detected) {
    pio_sm_set_enabled(pio0, 0, true);
  }
#else
  if (sm0_was_enabled) {
    pio_sm_set_enabled(pio0, 0, true);
  }
#endif
}

inline const char* FirmwareProductName() {
#if defined(_BOARDS_CENTIPEDE32Z_H)
  return "centipede32z";
#else
  return "tfr911h";
#endif
}

inline void AppendRawReport(Tcl_Interp* interp, const FullReport& rep) {
  char line[160];
  snprintf(line, sizeof(line),
           "META fw=%s id=%s sys_mhz=%lu burst_us=%lu eq_q=%u,%u,%u,%u eq_seq=%u,%u,%u\n",
           FirmwareProductName(), rep.pico_id,
           (unsigned long)rep.sys_mhz, (unsigned long)rep.burst_dt_us,
           rep.eq_state_counts[0], rep.eq_state_counts[1],
           rep.eq_state_counts[2], rep.eq_state_counts[3],
           rep.eq_quad_fwd, rep.eq_quad_rev, rep.eq_quad_diag);
  Tcl_AppendResult(interp, line, (char*)NULL);

  for (uint p = 0; p < kNumGpios; p++) {
    const PinDiag& d = rep.pins[p];
    snprintf(line, sizeof(line),
             "PIN %02u %-8s pd=%u pu=%u lf=%u lh=%u hf=%u hh=%u edges=%u hz=%lu duty=%u\n",
             p, PinClassName(d.cls), d.pd, d.pu, d.lf, d.lh, d.hf, d.hh,
             d.burst_edges, (unsigned long)d.freq_hz, d.duty_pct);
    Tcl_AppendResult(interp, line, (char*)NULL);
  }

  snprintf(line, sizeof(line), "SHORT count=%u", rep.num_shorts);
  Tcl_AppendResult(interp, line, (char*)NULL);
  for (uint i = 0; i < rep.num_shorts; i++) {
    snprintf(line, sizeof(line), " %u-%u", rep.shorts[i].a, rep.shorts[i].b);
    Tcl_AppendResult(interp, line, (char*)NULL);
  }
  Tcl_AppendResult(interp, "\n", (char*)NULL);

  snprintf(line, sizeof(line),
           "CPU ran=%u drv_abus=%04X rst_abus=%04X ctrl_drv=%X ctrl_val=%X fffe=%u ffff=%u\n",
           rep.cpu.ran ? 1u : 0u, rep.cpu.drv_abus_mask, rep.cpu.rst_abus_val,
           rep.cpu.ctrl_drv_mask, rep.cpu.ctrl_rst_val,
           rep.cpu.saw_fffe ? 1u : 0u, rep.cpu.saw_ffff ? 1u : 0u);
  Tcl_AppendResult(interp, line, (char*)NULL);
}

inline void AppendHumanReport(Tcl_Interp* interp, const FullReport& rep) {
  char line[192];
  snprintf(line, sizeof(line),
           "=== RP2350B 48-GPIO Diagnostic (FW=%s, ID=%s, %lu MHz) ===\n",
           FirmwareProductName(), rep.pico_id, (unsigned long)rep.sys_mhz);
  Tcl_AppendResult(interp, line, (char*)NULL);

  // Print compact 4-column table (GPIO 0..47):
  for (uint row = 0; row < 12; row++) {
    line[0] = '\0';
    int n = 0;
    for (uint col = 0; col < 4; col++) {
      uint p = col * 12 + row;
      const PinDiag& d = rep.pins[p];
      if (d.freq_hz >= 1000) {
        n += snprintf(line + n, sizeof(line) - n, "GP%02u:%-8s%4luk ",
                      p, PinClassName(d.cls), (unsigned long)((d.freq_hz + 500) / 1000));
      } else {
        n += snprintf(line + n, sizeof(line) - n, "GP%02u:%-13s ", p, PinClassName(d.cls));
      }
    }
    snprintf(line + n, sizeof(line) - n, "\n");
    Tcl_AppendResult(interp, line, (char*)NULL);
  }

  if (rep.pins[21].freq_hz > 0 || rep.pins[22].freq_hz > 0) {
    snprintf(line, sizeof(line),
             "Clocks: GP21(EBAR)=%lu Hz (%u%%), GP22(QBAR)=%lu Hz (%u%%), quad fwd=%u rev=%u diag=%u\n",
             (unsigned long)rep.pins[21].freq_hz, rep.pins[21].duty_pct,
             (unsigned long)rep.pins[22].freq_hz, rep.pins[22].duty_pct,
             rep.eq_quad_fwd, rep.eq_quad_rev, rep.eq_quad_diag);
    Tcl_AppendResult(interp, line, (char*)NULL);
  }

  if (rep.cpu.ran) {
    snprintf(line, sizeof(line),
             "63x09E Socket: drv_abus=0x%04X rst_abus=0x%04X ctrl_drv=0x%X vector_fffe_ffff=%s\n",
             rep.cpu.drv_abus_mask, rep.cpu.rst_abus_val, rep.cpu.ctrl_drv_mask,
             (rep.cpu.saw_fffe && rep.cpu.saw_ffff) ? "YES" : "NO");
    Tcl_AppendResult(interp, line, (char*)NULL);
  }

  if (rep.num_shorts == 0) {
    Tcl_AppendResult(interp, "Shorts: NONE across HI_Z pins\n", (char*)NULL);
  } else {
    Tcl_AppendResult(interp, "Shorts:", (char*)NULL);
    for (uint i = 0; i < rep.num_shorts; i++) {
      snprintf(line, sizeof(line), " GP%u<->GP%u", rep.shorts[i].a, rep.shorts[i].b);
      Tcl_AppendResult(interp, line, (char*)NULL);
    }
    Tcl_AppendResult(interp, "\n", (char*)NULL);
  }

  // Quick board/population inference:
  bool eq_clocks = (rep.pins[21].freq_hz >= 500000 && rep.pins[22].freq_hz >= 500000);
  bool eq_schmitt_idle = (!eq_clocks &&
      (rep.pins[21].cls == PC_DRV_HIGH || rep.pins[21].cls == PC_DRV_LOW) &&
      (rep.pins[22].cls == PC_DRV_HIGH || rep.pins[22].cls == PC_DRV_LOW));
  const char* verdict = "Unknown / Custom RP2350B board";
  if (eq_clocks) {
    verdict = "Centipede 32z (POPULATED: plugged into powered CoCo, E/Q clocks active)";
  } else if (eq_schmitt_idle) {
    verdict = "Centipede 32z (UNPOPULATED: unplugged from CoCo, Schmitt EBAR/QBAR drivers idle)";
  } else if (rep.cpu.ran && rep.cpu.drv_abus_mask == 0xFFFFu && rep.cpu.saw_fffe && rep.cpu.saw_ffff) {
    verdict = "TFR/911H (POPULATED: 63x09E/6809E CPU verified, RESET vector fetch OK)";
  } else if (rep.cpu.ran && rep.cpu.drv_abus_mask != 0) {
    verdict = "TFR/911H (PARTIAL CPU: some bus lines driven, check 63x09E socket seating)";
  } else if (rep.cpu.ran && rep.cpu.drv_abus_mask == 0) {
    verdict = "TFR/911H (UNPOPULATED: 63x09E socket empty, bus lines HI_Z)";
  }
  snprintf(line, sizeof(line), "Verdict: %s", verdict);
  Tcl_AppendResult(interp, line, (char*)NULL);
}

}  // namespace rp2350b_diag

inline int rp2350b_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  static rp2350b_diag::FullReport s_rep;
  if (argc > 1 && !strcmp(argv[1], "help")) {
    Tcl_AppendResult(interp,
        "rp2350b ?diag?       probe all 48 GPIOs, clocks, shorts & infer board/population\n"
        "rp2350b raw          emit machine-parsable report for scripts/diagnose_rp2350b.sh\n"
        "rp2350b pin <0..47>  probe a single GPIO (pulls, ~1-cycle @ 150MHz impulse, freq)\n"
        "rp2350b clocks       show active clock/toggling frequencies & GP21/GP22 quadrature\n"
        "rp2350b shorts       scan all HI_Z pins for pin-to-pin bridges",
        (char*)NULL);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "raw")) {
    rp2350b_diag::RunFullProbe(&s_rep, true, true);
    rp2350b_diag::AppendRawReport(interp, s_rep);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "shorts")) {
    rp2350b_diag::RunFullProbe(&s_rep, true, false);
    char buf[96];
    if (s_rep.num_shorts == 0) {
      Tcl_SetResult(interp, (char*)"0 pin-to-pin shorts across HI_Z GPIOs", TCL_STATIC);
    } else {
      Tcl_AppendResult(interp, "SHORT(S) DETECTED:", (char*)NULL);
      for (uint i = 0; i < s_rep.num_shorts; i++) {
        snprintf(buf, sizeof(buf), " GP%u<->GP%u", s_rep.shorts[i].a, s_rep.shorts[i].b);
        Tcl_AppendResult(interp, buf, (char*)NULL);
      }
    }
    return s_rep.num_shorts == 0 ? TCL_OK : TCL_ERROR;
  }
  if (argc > 1 && !strcmp(argv[1], "clocks")) {
    rp2350b_diag::RunFullProbe(&s_rep, false, false);
    char line[128];
    int found = 0;
    for (uint p = 0; p < rp2350b_diag::kNumGpios; p++) {
      if (s_rep.pins[p].freq_hz > 0) {
        snprintf(line, sizeof(line), "GP%02u: %lu Hz (duty %u%%, burst_edges %u)%s",
                 p, (unsigned long)s_rep.pins[p].freq_hz,
                 s_rep.pins[p].duty_pct, s_rep.pins[p].burst_edges,
                 "\n");
        Tcl_AppendResult(interp, line, (char*)NULL);
        found++;
      }
    }
    if (!found) Tcl_SetResult(interp, (char*)"no toggling GPIOs detected", TCL_STATIC);
    return TCL_OK;
  }
  if (argc > 1 && !strcmp(argv[1], "pin")) {
    if (argc < 3) {
      Tcl_SetResult(interp, (char*)"usage: rp2350b pin <0..47>", TCL_STATIC);
      return TCL_ERROR;
    }
    int p = atoi(argv[2]);
    if (p < 0 || p >= (int)rp2350b_diag::kNumGpios) {
      Tcl_SetResult(interp, (char*)"GPIO must be 0..47", TCL_STATIC);
      return TCL_ERROR;
    }
    rp2350b_diag::RunFullProbe(&s_rep, false, false);
    const auto& d = s_rep.pins[p];
    char buf[160];
    snprintf(buf, sizeof(buf),
             "GP%02d: %s pd=%u pu=%u low_pulse(fast=%u,20us=%u) high_pulse(fast=%u,20us=%u) freq=%lu Hz duty=%u%%",
             p, rp2350b_diag::PinClassName(d.cls), d.pd, d.pu,
             d.lf, d.lh, d.hf, d.hh, (unsigned long)d.freq_hz, d.duty_pct);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
  }
  if (argc == 1 || !strcmp(argv[1], "diag")) {
    rp2350b_diag::RunFullProbe(&s_rep, true, true);
    rp2350b_diag::AppendHumanReport(interp, s_rep);
    return TCL_OK;
  }
  Tcl_AppendResult(interp, "unknown subcommand \"", argv[1],
                   "\": try 'rp2350b help'", (char*)NULL);
  return TCL_ERROR;
}
