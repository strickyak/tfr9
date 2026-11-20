#pragma once
// coco2_mb.h -- Emulated CoCo 2 motherboard peripherals for TFR/911H:
//   - MC6883 SAM ($FFC0..$FFDF): 16-bit control latch, 32K/64K TY all-RAM mode, P1 page bit,
//     VDG mode/base bits, and $FFF0..$FFFF -> $BFF0..$BFFF hardware vector aliasing.
//   - MC6821 PIA0 ($FF00..$FF1F, mirrored & 3): keyboard matrix sense, joystick comparator, 60 Hz VSYNC IRQ.
//   - MC6821 PIA1 ($FF20..$FF3F, mirrored & 3): 6-bit DAC, 1-bit SBS sound, VDG mode/CSS, 64K RAMSZ sense, CART FIRQ.
//   - 32 KB SRAM-cached ROM window ($8000..$FEFF) stored in ram[0x10000..0x17FFF] (zero .bss growth).
//
// Core-1 functions are marked IN_RAM / FORCE_INLINE with zero XIP flash calls.

#include <cstdint>
#include <cstring>

#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#ifndef IN_RAM
#define IN_RAM
#endif

extern unsigned char ram[256 * 1024];

// 32 KB SRAM ROM window ($8000..$FEFF -> ram[0x10000..0x17EFF], plus $BFF0..$BFFF at ram[0x13FF0..0x13FFF]).
// A 64 KB CoCo 2 only uses ram[0x00000..0x0FFFF] for guest RAM, so placing the ROM cache at ram + 0x10000
// keeps Core 1 reads in 1-cycle SRAM with zero extra .bss usage.
constexpr uint32_t kCoco2RomSramBase = 0x10000u;

struct Coco2PiaChip {
  uint8_t ora = 0;
  uint8_t ddra = 0;
  uint8_t cra = 0;
  uint8_t orb = 0;
  uint8_t ddrb = 0;
  uint8_t crb = 0;

  void Reset() {
    ora = 0;
    ddra = 0;
    cra = 0;
    orb = 0;
    ddrb = 0;
    crb = 0;
  }

  FORCE_INLINE uint8_t EffectiveOutA(uint8_t pullups = 0xFFu) const {
    return (uint8_t)((ora & ddra) | (pullups & ~ddra));
  }

  FORCE_INLINE uint8_t EffectiveOutB(uint8_t pullups = 0xFFu) const {
    return (uint8_t)((orb & ddrb) | (pullups & ~ddrb));
  }

  FORCE_INLINE bool IrqAsserted() const {
    const bool irqa = ((cra & 0x80u) && (cra & 0x01u)) || ((cra & 0x40u) && ((cra & 0x28u) == 0x08u));
    const bool irqb = ((crb & 0x80u) && (crb & 0x01u)) || ((crb & 0x40u) && ((crb & 0x28u) == 0x08u));
    return irqa || irqb;
  }

  FORCE_INLINE uint8_t Read(uint8_t reg, uint8_t ext_in_a, uint8_t ext_in_b) {
    switch (reg & 3u) {
      case 0:
        if ((cra & 0x04u) == 0) return ddra;
        cra &= 0x3Fu;  // Reading Peripheral Data A clears IRQA1 & IRQA2
        return (uint8_t)((ora & ddra) | (ext_in_a & ~ddra));
      case 1:
        return cra;    // Reading Control Register A does NOT clear IRQ flags
      case 2:
        if ((crb & 0x04u) == 0) return ddrb;
        crb &= 0x3Fu;  // Reading Peripheral Data B clears IRQB1 & IRQB2
        return (uint8_t)((orb & ddrb) | (ext_in_b & ~ddrb));
      default:
        return crb;    // Reading Control Register B does NOT clear IRQ flags
    }
  }

  FORCE_INLINE void Write(uint8_t reg, uint8_t val) {
    switch (reg & 3u) {
      case 0:
        if ((cra & 0x04u) == 0) ddra = val;
        else                    ora = val;
        break;
      case 1:
        cra = (uint8_t)((cra & 0xC0u) | (val & 0x3Fu));
        break;
      case 2:
        if ((crb & 0x04u) == 0) ddrb = val;
        else                    orb = val;
        break;
      default:
        crb = (uint8_t)((crb & 0xC0u) | (val & 0x3Fu));
        break;
    }
  }
};

inline Coco2PiaChip coco2_pia0;
inline Coco2PiaChip coco2_pia1;

// SAM 16-bit control state ($FFC0..$FFDF) + cached decode flags
inline volatile uint16_t coco2_sam_bits = 0;
inline volatile bool coco2_sam_ty = false;           // Bit 15 ($FFDE/$FFDF): 0 = 32K RAM + ROM, 1 = 64K all-RAM
inline volatile uint32_t coco2_sam_p1_offset = 0;    // Bit 10 ($FFD4/$FFD5): 0x0000 or 0x8000 in map type 0
inline volatile bool coco2_active = false;           // True when `rsbasic / coco2` is the active engine

// Live keyboard column->row probe table (0x7F = no keys pressed). Combined with keyboard_injector::probe_table
// when keyboard_injector is active.
inline volatile uint8_t coco2_kb_live_probe[256];

// Joystick axis target (0..63, default 32 -> GETJOY converges to centered 31).
inline volatile uint8_t coco2_joy_target = 32;

inline void coco2_kb_clear_live() {
  for (int i = 0; i < 256; i++) {
    coco2_kb_live_probe[i] = 0x7Fu;
  }
}

inline void coco2_kb_set_live_key(int col, int row, bool shift = false, bool clear = false) {
  uint8_t col_resp[8] = {0x7Fu, 0x7Fu, 0x7Fu, 0x7Fu, 0x7Fu, 0x7Fu, 0x7Fu, 0x7Fu};
  if (col >= 0 && col < 8 && row >= 0 && row < 7) {
    col_resp[col] &= (uint8_t)~(1u << row);
    if (shift) col_resp[7] &= (uint8_t)~(1u << 6);
    if (clear) col_resp[1] &= (uint8_t)~(1u << 6);
  }
  for (int probe = 0; probe < 256; probe++) {
    uint8_t sense = 0x7Fu;
    for (int c = 0; c < 8; c++) {
      if ((probe & (1 << c)) == 0) {
        sense &= col_resp[c];
      }
    }
    coco2_kb_live_probe[probe] = sense;
  }
}

inline void coco2_mb_reset() {
  coco2_pia0.Reset();
  coco2_pia1.Reset();
  coco2_sam_bits = 0;
  coco2_sam_ty = false;
  coco2_sam_p1_offset = 0;
  coco2_joy_target = 32;
  coco2_kb_clear_live();
}

// 60 Hz VSYNC tick (invoked from Timer60HzCallback on Core 0 -> must be IN_RAM).
// Sets PIA0 CB1 interrupt flag (bit 7 of $FF03).
static void IN_RAM coco2_60hz_tick() {
  coco2_pia0.crb |= 0x80u;
}

// Trigger PIA1 CB1 (CART auto-start FIRQ flag, bit 7 of $FF23).
static void IN_RAM coco2_assert_cart_flag() {
  coco2_pia1.crb |= 0x80u;
}

FORCE_INLINE bool IN_RAM coco2_irq_needed() {
  return coco2_pia0.IrqAsserted();
}

FORCE_INLINE bool IN_RAM coco2_firq_needed() {
  return coco2_pia1.IrqAsserted();
}

// Effective PIA1 Port B value seen by VDG (PB7..PB3 outputs; PB2=0 64K RAMSZ jumper; PB0=0 printer ready).
FORCE_INLINE uint8_t IN_RAM coco2_pia1_outb() {
  return coco2_pia1.EffectiveOutB(0x00u);
}

// Current VDG framebuffer base address and byte size from SAM + PIA1 registers.
inline void coco2_video_geometry(uint16_t* out_base, uint16_t* out_bytes) {
  uint16_t sam = coco2_sam_bits;
  uint8_t p1b = coco2_pia1_outb();
  uint16_t base = (uint16_t)(((sam >> 3) & 0x7Fu) << 9);
  uint16_t nbytes = 512;
  if ((p1b & 0x80u) != 0) {
    switch ((p1b >> 4) & 0x07u) {
      case 0: nbytes = 1024; break;  // CG1: 64x64x4
      case 1: nbytes = 1024; break;  // RG1: 128x64x2
      case 2: nbytes = 2048; break;  // CG2: 128x64x4
      case 3: nbytes = 1536; break;  // RG2 / PMODE 0: 128x96x2
      case 4: nbytes = 3072; break;  // CG3 / PMODE 1: 128x96x4
      case 5: nbytes = 3072; break;  // RG3 / PMODE 2: 128x192x2
      case 6: nbytes = 6144; break;  // CG6 / PMODE 3: 128x192x4
      case 7: nbytes = 6144; break;  // RG6 / PMODE 4: 256x192x2
    }
  }
  if (out_base) *out_base = base;
  if (out_bytes) *out_bytes = nbytes;
}

// ── Named IN_RAM I/O Handlers for $FF00..$FF3F, $FFC0..$FFDF, $FFF0..$FFFF ──

static unsigned char IN_RAM coco2_pia0_reader(unsigned int addr) {
  const uint8_t col_probe = coco2_pia0.EffectiveOutB(0xFFu);
  uint8_t sense = coco2_kb_live_probe[col_probe];
#ifdef KEYBOARD_INJECTOR_H_
  if (keyboard_injector::active) {
    sense &= keyboard_injector::probe_table[keyboard_injector::active_table_idx][col_probe];
  }
#endif
  // Bit 7 of $FF00 is the DAC/joystick comparator output (high while 6-bit DAC < target).
  const uint8_t dac6 = (uint8_t)(coco2_pia1.EffectiveOutA(0x01u) >> 2);
  const uint8_t comp = (dac6 < coco2_joy_target) ? 0x80u : 0x00u;
  return coco2_pia0.Read((uint8_t)(addr & 3u), (uint8_t)((sense & 0x7Fu) | comp), 0xFFu);
}

static void IN_RAM coco2_pia0_writer(unsigned int addr, unsigned char val) {
  coco2_pia0.Write((uint8_t)(addr & 3u), val);
}

static unsigned char IN_RAM coco2_pia1_reader(unsigned int addr) {
  // PA0 = 1 (RS-232 serial mark / idle); PB2 = 0 (64K RAMSZ jumper -> 16K/64K mode in bas13.rom); PB0 = 0 (printer ready).
  return coco2_pia1.Read((uint8_t)(addr & 3u), 0x01u, 0x00u);
}

static void IN_RAM coco2_pia1_writer(unsigned int addr, unsigned char val) {
  coco2_pia1.Write((uint8_t)(addr & 3u), val);
}

static unsigned char IN_RAM coco2_sam_reader(unsigned int) {
  return 0xFFu;  // SAM registers ($FFC0..$FFDF) are write-only on MC6883
}

static void IN_RAM coco2_sam_writer(unsigned int addr, unsigned char) {
  const uint32_t bit = (addr - 0xFFC0u) >> 1;
  uint16_t bits = coco2_sam_bits;
  if ((addr & 1u) != 0) {
    bits = (uint16_t)(bits | (1u << bit));
  } else {
    bits = (uint16_t)(bits & ~(1u << bit));
  }
  coco2_sam_bits = bits;
  coco2_sam_ty = (bits & 0x8000u) != 0;
  coco2_sam_p1_offset = (bits & (1u << 10)) ? 0x8000u : 0u;
}

// MC6883 SAM aliases $FFF0..$FFFF to $BFF0..$BFFF (Color BASIC ROM when TY=0, RAM when TY=1).
static unsigned char IN_RAM coco2_vector_reader(unsigned int addr) {
  const uint32_t off = addr & 0x0Fu;
  if (coco2_sam_ty) {
    return ram[0xBFF0u + off];
  }
  return ram[kCoco2RomSramBase + 0x3FF0u + off];
}

static void IN_RAM coco2_vector_writer(unsigned int addr, unsigned char val) {
  if (coco2_sam_ty) {
    ram[0xBFF0u + (addr & 0x0Fu)] = val;
  }
}

// Fast Core-1 memory bus decode below $FF00:
FORCE_INLINE unsigned char IN_RAM coco2_mem_read(unsigned int addr) {
  if (addr < 0x8000u) {
    return ram[coco2_sam_ty ? addr : (addr + coco2_sam_p1_offset)];
  }
  if (coco2_sam_ty) {
    return ram[addr];
  }
  return ram[kCoco2RomSramBase + (addr - 0x8000u)];
}

FORCE_INLINE void IN_RAM coco2_mem_write(unsigned int addr, unsigned char val) {
  if (addr < 0x8000u) {
    ram[coco2_sam_ty ? addr : (addr + coco2_sam_p1_offset)] = val;
    return;
  }
  if (coco2_sam_ty) {
    ram[addr] = val;
  }
  // Writes to $8000..$FEFF when TY==0 hit ROM and are ignored.
}
