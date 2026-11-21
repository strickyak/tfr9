#pragma once
// audio_sn76489.h -- SN76489 sound generator + keyclick/bell/beep PWM mixer.
//
// Default address in enhancement map `e1` and `coco2`/`coco3` `jamesbrown`: 0xFF70 (SN76489 data), 0xFF71 (control);
// on `tfr911h` `jamesbrown`: 0xFF41..0xFF42.
// Control register bits:
//   bit 0 (0x01): enable keyclicks on console input
//   bit 1 (0x02): enable terminal bell on BEL (0x07)
//   bit 7 (0x80): pass tether arrow/page keys (128..133) raw instead of CoCo arrow codes
//
// Core 1 only enqueues chip writes and sets request flags in SRAM.  Synthesis runs on core 0
// in a 31.25 kHz hardware alarm ISR (ALARM 2), initialized lazily when AudioSn76489::Install()
// or Tcl `beep` is first used.  `amp_table` is non-const so it resides in `.data` SRAM (never XIP flash).

#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/timer.h>

#ifndef AUDIO_PWM_PIN
#define AUDIO_PWM_PIN 8   // TFR/911H J3 pin 1 = GPIO 8 (Centipede 32z overrides or sets -1: S16)
#endif

namespace audio {

constexpr uint kAlarm = 2;             // default SDK alarm pool uses alarm 3
constexpr uint kSampleUs = 32;         // 31,250 Hz
constexpr uint kSampleHz = 1000000 / kSampleUs;
constexpr uint kChipHz = 3579545;      // NTSC SN76489 input clock
constexpr uint32_t kStep = (uint32_t)((double)kChipHz / 16.0 / kSampleHz * 65536.0);

// Placed in its own __not_in_flash data section so GCC cannot promote it to `.rodata` XIP flash.
static byte __not_in_flash("amp_table") amp_table[16] = {30, 24, 19, 15, 12, 9, 8, 6, 5, 4, 3, 2, 2, 1, 1, 0};

inline volatile byte control = 0x03;   // bit 0 click, bit 1 bell, bit 7 raw arrows
inline volatile byte fifo[256];
inline volatile byte fifo_head = 0;
inline volatile byte fifo_tail = 0;
inline volatile bool click_request = false;
inline volatile bool bell_request = false;
inline volatile bool beep_request = false;
inline volatile uint beep_hz = 880;
inline volatile uint beep_ms = 200;
inline volatile uint32_t stat_chip_writes = 0;
inline bool hw_inited = false;
inline bool installed = false;
inline uint16_t port_base = 0xFF70;

// Owned by core-0 alarm ISR:
static uint16_t tone_n[3] = {1024, 1024, 1024};
static byte atten[4] = {15, 15, 15, 15};
static byte noise_ctrl = 0;
static uint16_t lfsr = 0x4000;
static byte latch_ch = 0, latch_vol = 0;
static uint32_t count[4] = {0, 0, 0, 0};
static byte out[4] = {0, 0, 0, 0};
static uint fx_half = 1, fx_left = 0, fx_phase = 0;
static int fx_amp = 0, fx_decay_every = 0, fx_decay_count = 0;
static uint32_t next_alarm = 0;

FORCE_INLINE void IN_RAM WriteChip(byte b) {
  byte h = fifo_head;
  if ((byte)(h + 1) != fifo_tail) {
    fifo[h] = b;
    __dmb();
    fifo_head = (byte)(h + 1);
  }
}

FORCE_INLINE void IN_RAM Bell() {
  if (installed && (control & 0x02)) bell_request = true;
}

inline void Click() {
  if (installed && (control & 0x01)) click_request = true;
}

static void IN_RAM ApplyWrite(byte b) {
  if (b & 0x80) {
    latch_ch = (b >> 5) & 3;
    latch_vol = b & 0x10;
    byte data = b & 0x0F;
    if (latch_vol) {
      atten[latch_ch] = data;
    } else if (latch_ch == 3) {
      noise_ctrl = data & 7;
      lfsr = 0x4000;
    } else {
      uint n = (tone_n[latch_ch] & 0x3F0) | data;
      tone_n[latch_ch] = n ? n : 1024;
    }
  } else {
    if (latch_vol) {
      atten[latch_ch] = b & 0x0F;
    } else if (latch_ch == 3) {
      noise_ctrl = b & 7;
      lfsr = 0x4000;
    } else {
      uint n = (tone_n[latch_ch] & 0x00F) | ((b & 0x3F) << 4);
      tone_n[latch_ch] = n ? n : 1024;
    }
  }
}

static void IN_RAM StartFx(uint hz, uint ms, int amp, int decay_every) {
  fx_half = kSampleHz / (2 * (hz ? hz : 1));
  if (fx_half == 0) fx_half = 1;
  fx_left = (kSampleHz / 1000) * ms;
  fx_phase = 0;
  fx_amp = amp;
  fx_decay_every = decay_every;
  fx_decay_count = 0;
}

FORCE_INLINE int IN_RAM Voice(uint i, uint period_ticks) {
  if (period_ticks < 8) {
    out[i] = 1;
  } else {
    count[i] += kStep;
    uint32_t p = period_ticks << 16;
    if (count[i] >= p) {
      count[i] -= p;
      out[i] ^= 1;
      if (i == 3 && out[i]) {
        uint fb = (noise_ctrl & 4) ? ((lfsr ^ (lfsr >> 1)) & 1) : (lfsr & 1);
        lfsr = (uint16_t)((lfsr >> 1) | (fb << 14));
      }
    }
  }
  int a = amp_table[atten[i]];
  if (i == 3) return (lfsr & 1) ? a : -a;
  return out[i] ? a : -a;
}

static void IN_RAM Interrupt() {
  timer_hw->intr = 1u << kAlarm;
  next_alarm += kSampleUs;
  uint32_t now = timer_hw->timerawl;
  if ((int32_t)(next_alarm - now) <= 1) next_alarm = now + kSampleUs;
  timer_hw->alarm[kAlarm] = next_alarm;

  while (fifo_tail != fifo_head) {
    ApplyWrite(fifo[fifo_tail]);
    stat_chip_writes++;
    fifo_tail = (byte)(fifo_tail + 1);
  }
  if (beep_request) {
    beep_request = false;
    StartFx(beep_hz, beep_ms, 28, 0);
  } else if (bell_request) {
    bell_request = false;
    StartFx(880, 150, 28, 0);
  } else if (click_request) {
    click_request = false;
    StartFx(3900, 2, 40, 2);
  }

  int s = 128;
  s += Voice(0, tone_n[0]);
  s += Voice(1, tone_n[1]);
  s += Voice(2, tone_n[2]);
  uint nr = noise_ctrl & 3;
  s += Voice(3, nr == 3 ? tone_n[2] : (16u << nr));
  if (fx_left) {
    fx_left--;
    if (++fx_phase >= 2 * fx_half) fx_phase = 0;
    s += (fx_phase < fx_half) ? fx_amp : -fx_amp;
    if (fx_decay_every && ++fx_decay_count >= fx_decay_every) {
      fx_decay_count = 0;
      if (fx_amp > 0) fx_amp--;
    }
  }
  if (s < 0) s = 0;
  if (s > 255) s = 255;
#if AUDIO_PWM_PIN >= 0
  pwm_set_gpio_level((uint)AUDIO_PWM_PIN, (uint16_t)s);
#endif
}

inline void EnsureInit() {
  if (hw_inited) return;
  hw_inited = true;
#if AUDIO_PWM_PIN >= 0
  gpio_set_function((uint)AUDIO_PWM_PIN, GPIO_FUNC_PWM);
  uint slice = pwm_gpio_to_slice_num((uint)AUDIO_PWM_PIN);
  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_wrap(&cfg, 255);
  pwm_config_set_clkdiv(&cfg, 1.0f);
  pwm_init(slice, &cfg, true);
  for (uint level = 0; level <= 128; level += 8) {
    pwm_set_gpio_level((uint)AUDIO_PWM_PIN, level);
    sleep_us(200);
  }
#endif
  hardware_alarm_claim(kAlarm);
  uint irq = hardware_alarm_get_irq_num(kAlarm);
  irq_set_exclusive_handler(irq, Interrupt);
  hw_set_bits(&timer_hw->inte, 1u << kAlarm);
  irq_set_enabled(irq, true);
  next_alarm = timer_hw->timerawl + kSampleUs;
  timer_hw->alarm[kAlarm] = next_alarm;
}

inline void Beep(uint hz, uint ms) {
  EnsureInit();
  beep_hz = hz;
  beep_ms = ms;
  __dmb();
  beep_request = true;
}

}  // namespace audio

byte IN_RAM audio_read_chip(uint) { return 0; }
void IN_RAM audio_write_chip(uint, byte d) { audio::WriteChip(d); }
byte IN_RAM audio_read_ctrl(uint) { return audio::control; }
void IN_RAM audio_write_ctrl(uint, byte d) { audio::control = d; }

struct AudioSn76489 {
  static constexpr uint16_t kDefaultBase = 0xFF70;
  static void Install(uint16_t base = kDefaultBase) {
    audio::EnsureInit();
    audio::port_base = base;
    audio::installed = true;
    audio::control = 0x03;
    IoClaim(base, base + 1, "audio");
    IoSet(base + 0, audio_read_chip, audio_write_chip);
    IoSet(base + 1, audio_read_ctrl, audio_write_ctrl);
  }
};

inline int beep_cmd(ClientData, Tcl_Interp*, int argc, char* argv[]) {
  uint hz = argc > 1 ? (uint)atoi(argv[1]) : 880;
  uint ms = argc > 2 ? (uint)atoi(argv[2]) : 200;
  audio::Beep(hz, ms);
  return TCL_OK;
}

inline int click_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc > 1) {
    if (!strcmp(argv[1], "off")) audio::control &= ~0x01;
    else audio::control |= 0x01;
  }
  Tcl_SetResult(interp, (char*)((audio::control & 0x01) ? "on" : "off"), TCL_STATIC);
  return TCL_OK;
}
