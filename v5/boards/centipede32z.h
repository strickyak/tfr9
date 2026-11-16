#ifndef _BOARDS_CENTIPEDE32Z_H
#define _BOARDS_CENTIPEDE32Z_H

#ifdef __cplusplus
extern "C" {
#endif

// Copico Centipede 32z (Solder Party RP2350 Stamp XL, RP2350B 48-GPIO QFN-80, 16 MB QSPI flash)
#define PICO_DEFAULT_LED_PIN 25
#define PICO_RP2350A 0

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// Centipede 32z CoCo 40-pin cartridge interface GPIOs:
//   GPIO 0..7   : Data bus D0..D7
//   GPIO 8      : G_CTS  (Cartridge Select $C000-$FEFF, active low input) -- NEVER drive PWM here!
//   GPIO 9      : G_SCS  (Spare Chip Select $FF40-$FF5F, active low input)
//   GPIO 10..11 : Orchestra-90 stereo PWM DAC
//   GPIO 12..13 : UART0 TX/RX (FujiNet / Becker bridge)
//   GPIO 20     : G_RW   (6809 R/W, high = read, low = write)
//   GPIO 21     : G_E    (EBAR, inverted 6809 E clock)
//   GPIO 22     : G_Q    (QBAR, inverted 6809 Q clock)
//   GPIO 25     : G_LED  (on-board status LED)
//   GPIO 26     : G_SND  (CoCo cartridge SND audio input -> TV/monitor speaker)
//   GPIO 27     : G_CART (CoCo cartridge CART -> PIA1 CB1 FIRQ, open-drain)
//   GPIO 28     : G_SLENB (CoCo cartridge SLENB#, PIO side-set open-drain)
//   GPIO 29     : G_HALT (6809 HALT#, open-drain)
//   GPIO 30     : G_NMI  (6809 NMI#, open-drain)
//   GPIO 31     : G_RESET (CoCo RESET#, input / open-drain)
//   GPIO 32..47 : Address bus A0..A15 (sio_hw->gpio_hi_in & 0xFFFF)
#define G_CTS   8
#define G_SCS   9
#define G_RW    20
#define G_E     21
#define G_Q     22
#define G_LED   25
#define G_SND   26
#define G_CART  27
#define G_SLENB 28
#define G_HALT  29
#define G_NMI   30
#define G_RESET 31

// Route SN76489 audio enhancement PWM to CoCo cartridge SND pin (GPIO 26), never GPIO 8 (G_CTS) (spec S16).
#define AUDIO_PWM_PIN G_SND

#ifdef __cplusplus
}
#endif

#endif  // _BOARDS_CENTIPEDE32Z_H
