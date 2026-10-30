#ifndef V4_MULTICOMP09_H_
#define V4_MULTICOMP09_H_

#include <cstdint>
#include <cstring>
#include <atomic>
#include "hardware/sync.h"
#include "pico/util/queue.h"

#ifndef IN_RAM
#define IN_RAM __not_in_flash("tfr911")
#endif
#ifndef FORCE_INLINE
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

using byte = unsigned char;
extern byte ram[256 * 1024];
extern queue_t fg2bg_queue;

void Say(byte c);

// ═══════════════════════════════════════════════════════════════════
// Multicomp09 MMU & Banking State ($FFDE..$FFDF)
//
// 64 KB logical address space in eight 8 KB slots.
// Two 8-slot maps: Map 0 (Kernel) and Map 1 (User).
// 256 KB physical RAM on RP2350B = 32 blocks of 8 KB (pages 0..31).
// ═══════════════════════════════════════════════════════════════════

inline byte mc_map0[8];
inline byte mc_map1[8];
inline byte mc_tr = 0;
inline byte mc_slot = 0;
inline uint32_t mc_slot_base[8];

FORCE_INLINE void IN_RAM mc_update_mmu() {
  const byte* m = (mc_tr != 0) ? mc_map1 : mc_map0;
  for (int s = 0; s < 8; s++) {
    mc_slot_base[s] = (uint32_t)(m[s] & 0x1F) << 13;
  }
}

// ═══════════════════════════════════════════════════════════════════
// Multicomp09 ACIA 6850 Console UART ($FFD0..$FFD1)
// ═══════════════════════════════════════════════════════════════════

inline volatile byte mc_acia_ctrl = 0;
inline volatile byte mc_rx_char = 0;
inline volatile bool mc_rx_has_char = false;

static inline byte IN_RAM mc_read_acia_status() {
  byte st = 0x02; // TDRE always 1 (transmit data register empty)
  if (mc_rx_has_char) {
    st |= 0x01; // RDRF (receive data register full)
    if ((mc_acia_ctrl & 0x80) != 0) {
      st |= 0x80; // IRQ
    }
  }
  return st;
}

static inline byte IN_RAM mc_read_acia_data() {
  mc_rx_has_char = false;
  return mc_rx_char;
}

static inline void IN_RAM mc_write_acia_ctrl(byte d) {
  mc_acia_ctrl = d;
}

static inline void IN_RAM mc_write_acia_data(byte d) {
  Say(d);
}

// ═══════════════════════════════════════════════════════════════════
// Multicomp09 50Hz Timer ($FFDD)
// ═══════════════════════════════════════════════════════════════════

inline volatile bool mc_timer_en = false;
inline volatile bool mc_timer_pending = false;

static inline byte IN_RAM mc_read_timer() {
  byte val = 0;
  if (mc_timer_pending) val |= 0x80;
  if (mc_timer_en) val |= 0x02;
  mc_timer_pending = false;
  return val;
}

static inline void IN_RAM mc_write_timer(byte d) {
  mc_timer_en = (d & 0x02) != 0;
  mc_timer_pending = false;
}

FORCE_INLINE bool IN_RAM mc_irq_needed() {
  bool acia_irq = ((mc_acia_ctrl & 0x80) != 0) && mc_rx_has_char;
  return mc_timer_pending || acia_irq;
}

// ═══════════════════════════════════════════════════════════════════
// Multicomp09 SPI SD Controller ($FFD8..$FFDC)
// ═══════════════════════════════════════════════════════════════════

struct McSdRequest {
  uint32_t lba;
  uint8_t command; // 0=read, 1=write
  std::atomic<int8_t> status; // -1=pending, 0=ok, >0=error
};

inline McSdRequest g_mc_sd_req;
inline byte mc_sd_lba0 = 0;
inline byte mc_sd_lba1 = 0;
inline byte mc_sd_lba2 = 0;
inline volatile byte mc_sd_ctl = 0x80; // 0x80 = idle
inline byte mc_sd_buf[512];
inline uint16_t mc_sd_idx = 0;

void IN_RAM mc_dispatch_sd_cmd(uint8_t cmd);

static inline byte IN_RAM mc_read_sd_data() {
  if (mc_sd_ctl != 0xE0) return 0xFF;
  byte val = mc_sd_buf[mc_sd_idx++];
  if (mc_sd_idx >= 512) {
    mc_sd_idx = 0;
    mc_sd_ctl = 0x80; // Return to idle
  }
  return val;
}

static inline void IN_RAM mc_write_sd_data(byte d) {
  if (mc_sd_ctl != 0xA0) return;
  mc_sd_buf[mc_sd_idx++] = d;
  if (mc_sd_idx >= 512) {
    mc_sd_idx = 0;
    mc_dispatch_sd_cmd(0x01); // Complete sector write (sets mc_sd_ctl = 0x80 on finish)
  }
}

// ═══════════════════════════════════════════════════════════════════
// Unified Multicomp09 IO Dispatch ($FFD0..$FFDF)
// ═══════════════════════════════════════════════════════════════════

static inline byte IN_RAM mc_read_io(uint16_t addr) {
  switch (addr) {
    case 0xFFD0: return mc_read_acia_status();
    case 0xFFD1: return mc_read_acia_data();
    case 0xFFD2: return 0x02; // UART 1 Status
    case 0xFFD3: return 0x00; // UART 1 Data
    case 0xFFD4: return 0x02; // UART 2 Status (DriveWire)
    case 0xFFD5: return 0x00; // UART 2 Data

    case 0xFFD8: return mc_read_sd_data();
    case 0xFFD9: return mc_sd_ctl;
    case 0xFFDA: return mc_sd_lba0;
    case 0xFFDB: return mc_sd_lba1;
    case 0xFFDC: return mc_sd_lba2;

    case 0xFFDD: return mc_read_timer();

    case 0xFFDE: // MMUADR
      return (mc_tr << 6) | (mc_slot & 0x0F) | 0x80 | 0x20;
    case 0xFFDF: // MMUDAT
      return (mc_slot < 8) ? mc_map0[mc_slot] : mc_map1[mc_slot - 8];

    default:
      return 0xFF;
  }
}

static inline void IN_RAM mc_write_io(uint16_t addr, byte val) {
  switch (addr) {
    case 0xFFD0: mc_write_acia_ctrl(val); break;
    case 0xFFD1: mc_write_acia_data(val); break;
    case 0xFFD2: case 0xFFD3: case 0xFFD4: case 0xFFD5: break;

    case 0xFFD8: mc_write_sd_data(val); break;
    case 0xFFD9: mc_dispatch_sd_cmd(val); break;
    case 0xFFDA: mc_sd_lba0 = val; break;
    case 0xFFDB: mc_sd_lba1 = val; break;
    case 0xFFDC: mc_sd_lba2 = val; break;

    case 0xFFDD: mc_write_timer(val); break;

    case 0xFFDE: // MMUADR
      mc_slot = val & 0x0F;
      mc_tr = (val >> 6) & 1;
      mc_update_mmu();
      break;

    case 0xFFDF: // MMUDAT
      if (mc_slot < 8) {
        mc_map0[mc_slot] = val & 0x7F;
      } else {
        mc_map1[mc_slot - 8] = val & 0x7F;
      }
      mc_update_mmu();
      break;

    default:
      break;
  }
}

#endif // V4_MULTICOMP09_H_
