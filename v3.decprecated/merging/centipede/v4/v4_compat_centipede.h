#ifndef V4_COMPAT_CENTIPEDE_H_
#define V4_COMPAT_CENTIPEDE_H_

// v4_compat_centipede.h — Compatibility shim for including v1 headers in v4.
//
// This header bridges the naming and macro conventions between the v4
// framework and the v1 Centipede firmware headers. Include this AFTER
// the v4 framework headers and BEFORE any v1 headers.
//
// It provides:
//   - G_* pin macros mapped to centipede_pins:: namespace values
//   - ASSERT_NMI/RELEASE_NMI/ASSERT_HALT/RELEASE_HALT macros
//   - GERBIL_GET/GERBIL_DRIVE/GERBIL_PASS macros
//   - SamP1Bit, SamTyBit globals
//   - nmi_pending volatile flag
//   - centipede_config struct and global
//   - USB pipeline infrastructure (CircBuf, CobsDecoder, UsbReceiver)
//   - SET_LED, volatile_sio_hw macros
//   - Helper functions (INPUT, OUTPUT, HaltOn, HaltOff, etc.)

// ── GPIO Pin Aliases ──
// Map G_* macros to the centipede_pins:: constexpr values.
#define G_RW    centipede_pins::RW
#define G_E     centipede_pins::EBAR
#define G_Q     centipede_pins::QBAR
#define G_LED   centipede_pins::LED
#define G_SCS   centipede_pins::SCS
#define G_CART  centipede_pins::CART
#define G_SLENB centipede_pins::SLENB
#define G_HALT  centipede_pins::HALT
#define G_NMI   centipede_pins::NMI
#define G_CTS   centipede_pins::CTS
#define G_RESET centipede_pins::RESET
#define G_D0    0
#define G_A0    32

// ── LED and SIO macros ──
#define SET_LED(X) gpio_put(G_LED, (X))
#define volatile_sio_hw ((volatile sio_hw_t*)SIO_BASE)

// ── Helper Functions ──
// Used by v1 code for GPIO setup and HALT control.
inline void INPUT(int i) {
  gpio_init(i);
  gpio_set_dir(i, GPIO_IN);
  gpio_set_pulls(i, false, false);
}
inline void OUTPUT(int i, int x) {
  gpio_init(i);
  gpio_set_dir(i, GPIO_OUT);
  gpio_put(i, x);
}

inline void HaltOn() { gpio_set_dir(G_HALT, GPIO_OUT); }
inline void HaltOff() { gpio_set_dir(G_HALT, GPIO_IN); }

// ── NMI Handling ──
// NMI is edge-triggered on the 6809. Assert on foreground, set flag.
// Background's drain_task checks and releases promptly.
inline volatile bool nmi_pending = false;
#define ASSERT_NMI() do { \
    gpio_set_dir(G_NMI, GPIO_OUT); \
    nmi_pending = true; \
  } while(0)
#define RELEASE_NMI() gpio_set_dir(G_NMI, GPIO_IN)

// ── HALT macros ──
#define ASSERT_HALT() gpio_set_dir(G_HALT, GPIO_OUT)
#define RELEASE_HALT() gpio_set_dir(G_HALT, GPIO_IN)

// ── Gerbil PIO Macros ──
// These will be defined once the PIO program is included.
// For now, declare pio and sm as extern so the macros compile.
// The actual PIO initialization sets these in the foreground setup.
inline PIO pio = pio0;
inline uint sm = 0;

// These macros wrap the Gerbil PIO FIFO operations.
// They are defined as inline functions to allow the gerbil.pio.h
// generated header to provide the actual implementation.
// GERBIL_GET(), GERBIL_DRIVE(x), GERBIL_PASS() will be #defined
// after gerbil.pio.h is included in the main file.

// ── SAM bits (for coco64k.h) ──
inline bool SamP1Bit = false;
inline bool SamTyBit = false;

// ── Centipede Configuration ──
struct CentipedeConfig {
  bool ram_64k;
  bool rom_disk11;
  bool floppy_fd;
  bool floppy_pc;
  bool trace_writes;
  bool trace_reads;

  void SetAll(bool b) {
    ram_64k = b;
    rom_disk11 = b;
    floppy_fd = b;
    floppy_pc = b;
    trace_writes = b;
    trace_reads = b;
  }
};
inline CentipedeConfig centipede_config;

// ── USB Pipeline ──
// USB receive pipeline using CircBuf from v1/util/circbuf.h.
// These globals are used by the COBS decoder and USB receiver.
#include "../v1/util/circbuf.h"
#include "../v1/util/cobs.h"

// Forward-declare usb_pipeline types. The actual #include of
// usb_pipeline.h happens after this header, as it depends on CircBuf.

// ── E clock detection ──
inline std::atomic<bool> startup_e_clock_detected{false};

inline bool IN_RAM detect_e_clock() {
  uint count_high = 0, count_low = 0, transitions = 0;
  bool last_state = gpio_get(G_E);
  for (uint i = 0; i < 10000; i++) {
    bool current_state = gpio_get(G_E);
    if (current_state)
      count_high++;
    else
      count_low++;
    if (current_state != last_state) {
      transitions++;
      last_state = current_state;
    }
  }
  bool ok = count_high > 200 && count_low > 200 && transitions >= 10;
  if (ok) startup_e_clock_detected = true;
  return ok;
}

// ── Boot mode (for flash-restart detection) ──
#define BOOT_MODE_CHECKER 0x56781234u
extern uint32_t __uninitialized_ram(boot_mode);
extern uint32_t __uninitialized_ram(boot_mode_check);

#endif  // V4_COMPAT_CENTIPEDE_H_
