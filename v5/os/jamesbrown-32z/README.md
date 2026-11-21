# `jamesbrown-32z` (NitrOS-9 Level 2 + FujiNet + Basic09 for CoCo 2 / CoCo 3)

CoCo-2 and CoCo-3 compatible Motorola 6809E edition of Andrew Diller's **JamesBrown** NitrOS-9 Level 2 + FujiNet + Unix commands + Games + `basic09` release, built into `build/centipede32z-jamesbrown.dsk` by `scripts/make-jamesbrown-32z-dsk.sh` (`make jamesbrown-32z-dsk`) and launched by `./run-centipede32z-jamesbrown.sh`.

## I/O Address Relocation (`on coco2|coco3 run nitros9l2 for jamesbrown`)

All ports that clashed with CoCo motherboard `PIA0`/`PIA1` (`$FF00–$FF3F`) or `SAM` (`$FFC0–$FFDF`) move to their standard CoCo-safe `e1` / `coco3` homes:

| Device | TFR/911H `jamesbrown` | `jamesbrown-32z` (`coco2` / `coco3`) | Notes |
|---|---|---|---|
| Console ACIA (`sc6850`, `/Term`) | `$FF14–$FF15` | `$FF88–$FF89` | Polled 60x/sec via `D.Poll` |
| FujiNet Becker (`dwio`, `boot_dw`, `fn*`) | `$FF45–$FF46` | `$FF41–$FF42` | `UART0` GPIO 12/13 @ 921,600 baud |
| SN76489 + Ctrl (`click`, `vi`, `tetris`, `zm`) | `$FF41–$FF42` | `$FF70–$FF71` | PWM on `G_SND` (GPIO 26); `$FF71` bit 7 = raw arrows |
| Wall-Clock `RtcPort` (`clock2_e1`) | `$FFC0–$FFC7` | `$FF72–$FF79` | Synced from host PC via `tether` `T_WALLTIME` |
| Firmware `InfoPort` (`info`) | `$FF43–$FF44` | `$FF7C–$FF7D` | Write `$A5` to `$FF7C`, poll `1`, read `$FF7D` |
| Non-blocking FIFO `EmuDsk` (`/DD`, `/H0`) | `$FF80–$FF86` | `$FF80–$FF86` | Commands `$10` (read), `$11` (write reset), `$12` (write commit) |
| GIME / MMU / Palette / Vectors | `$FF90–$FF93`, `$FFA0–$FFAF`, `$FFF0–$FFFF` | `$FF90–$FFBF`, `$FFF0–$FFFF` | `gerbil_gime` (256 KB SRAM) on `coco2`; real CoCo 3 motherboard under `gerbil_io` on `coco3` |

## Hardware Adaptations in `modules/`

1. **Strict Motorola 6809E (`--6809 -DH6309=0`):** All kernel, driver, utility, FujiNet, and C modules compile strictly for `MC6809E` (no 6309 opcodes).
2. **`rel.asm` (SAM `$FFD8` 0.895 MHz):** Keeps SAM `R1=0` (`$FFD8`) instead of `$FFD9` so a 16KB CoCo 2 preserves VDG refresh and keeps its `MC6809E` within spec. Exact `$130`-byte image size (`$ED00..$EE2F`).
3. **`boot_emu.asm` & `emudsk.asm` (Non-Blocking Byte-FIFO `EmuDsk`):** Because a real CoCo's external `E`/`Q` clock keeps running during USB `/pc` sector I/O, `boot_emu` (exact `$1D0` bytes, `$EE30..$EFFF`) and `emudsk` use non-blocking FIFO commands (`$10` read, `$11` write reset, `$12` write commit), polling bit 7 (`$80` = busy) of `$FF83` and streaming 256 bytes via `$FF84` (read) / `$FF85` (write). This also works on real CoCo 3 motherboard DRAM (`gerbil_io`).
4. **`clock.asm` & `clock2_e1.asm` (CoCo `PIA0` 60 Hz VSYNC `_IRQ` + `DoPoll`):** Since the 40-pin CoCo cartridge port has no `_IRQ` pin, `Clock` enables motherboard `PIA0` CB1 60 Hz VSYNC `_IRQ` (`$FF03 = $35`), disables GIME VBORD IRQs (`$FF92 = $00`) to prevent double-ticks on CoCo 3, links `Clock2` (`$FF72–$FF79`), and calls `DoPoll` every tick to service `sc6850` (`/Term` at `$FF88`).
5. **`dwread_becker_to.asm`:** 6809 Becker `DWRead` with a ~1.75s safety timeout so an unplugged hat or stalled upstream HTTP server returns `E$Read` (`$F4`) instead of spinning forever with interrupts masked.
