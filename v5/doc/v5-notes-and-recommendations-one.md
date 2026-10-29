# TFR9 `v5` Architecture Notes, Discoveries, Snags, and Recommendations

This document synthesizes a complete audit of:
1. **`../v4/`** (`/home/strick/modoc/coco-shelf/tfr9/v4/`) across its three fragmented sub-trees (`tfr911h` + `firmware/one`, `centipede-32z` + `three`, and `turbolab`),
2. **Andrew Diller's last 15 commits** (`b009255..77ad30c` on branch `nitros9-l2-tfr911` in `~/github.com/dillera/tfr9`), and
3. **Hardware schematics and KiCad PCB layouts** for `tfr911h`, `tfr911k`, `rp2350b-add-psram-a`/`b`, `centipede-32z`, and `hat-fuji-40c`/`41a`/`41d`.

It catalogues critical bugs and "snags for the unwary" discovered during this audit and proposes a unified, modular directory and software architecture for **`v5/`** where common RP2350B infrastructure (LittleFS, Tcl 6.7c, VFS, COBS/PicoRPC, composable 6809/6309 I/O & MMU emulation) and the Go `tether` tool are shared cleanly without touching or depending on `../v4/`.

---

## 1. The Five Pillars and Operating Systems of `v5/`

The `v5/` repository must support five hardware/workflow targets and five families of 6809/6309 operating systems from a single coherent codebase:

### 1.1 Hardware & Workflow Pillars
1. **`tfr911h` Single Board Computer (SBC)**:
   - RP2350B microcontroller (`U1`, 80-pin QFN, 48 GPIOs) + physical 40-pin DIP MC6809E or HD63C09E CPU (`U5`) + 16 MB Winbond QSPI Flash (`U4`) + 2x10 expansion/FujiNet header (`J3`).
   - Future-proofed for `rp2350b-add-psram-a`/`b` (2 MB / 8 MB QSPI PSRAM with `GPIO19` as CS).
   - RP2350B Core 1 generates quadrature `E`/`Q` clocks via `hamster.pio` and emulates all RAM, ROM, MMU, and memory-mapped I/O on the 6809/6309 bus.
2. **`Copico Centipede 32Z` Cartridge for CoCo 1, 2, or 3**:
   - SolderParty RP2350B Stamp XL (`U1`) + 74LVC245 bus transceivers on the 40-pin Tandy Color Computer cartridge port + 2x10 expansion/FujiNet header (`J3`).
   - RP2350B Core 1 slaved to the CoCo's `EBAR`/`QBAR` clocks via `gerbil.pio`, asserting open-drain `SLENB*` (GPIO 28) when overriding internal CoCo RAM/ROM/I/O, or passively sniffing bus reads/writes (`GERBIL_PASS`) to shadow video/SAM/MMU state.
   - Supports on-CoCo "spoonfed" PMODE 4 Tcl boot menu (`gspoon.h`, feeding `JMP $7E7E` to the CoCo 6809 while rendering text/menus on the CoCo's own screen and reading the CoCo keyboard), plus `centiscope` passive logic analyzer mode.
3. **`TurboLab` Student Environment for `tfr911h`**:
   - Streamlined student/classroom setup running **TurboOS** (and OS-9/6809 experiments) on the `tfr911h`.
   - Provides deterministic cycle-accurate tracing (`--trace`), relocated assembly listing correlation (`.list`), OS-9 `SWI2`/`RTI` system call decoding, hardware watchpoints (`--watch`, `--trigger`, `--stop`), runtime PIO clock/timing tuning (`--tuning`), and `SWI`-injected post-mortem register/core dumps (`C_FAULT` + `C_CORE_DUMP`).
4. **FujiNet on CoCo (via Centipede 32Z) and on `tfr911h`**:
   - Uses the **`hat-fuji-41d`** (or bodged `hat-fuji-40c`) ESP32-DevKitC daughterboard plugged into the 2x10 `J3` socket present on **both** `centipede-32z` and `tfr911h`.
   - RP2350B bridges 6809 memory-mapped I/O (Becker port) to the ESP32 running `fujinet-firmware` over high-speed UART (or SPI) on `GPIO12..15`.
5. **Operating Systems & Runtime Environments**:
   - **TurboOS**: Minimal ROM/RAM OS-9-style system for `tfr911h` and `TurboLab`.
   - **NitrOS-9 Level 1 & Level 2**:
     - Level 1 (`flat65280` 64 KB on `tfr911h`; CoCo 2 64 KB + virtual floppy/HDB-DOS on `centipede-32z`).
     - Level 2 (`deep65280` 2 MB banked RAM with CoCo 3 MMU on `tfr911h`; CoCo 3 128 KB/512 KB on `centipede-32z`), including Andrew Diller's NitrOS-9 Level 2 + FujiNet software suite (`scdwv`/`SS.Fuji`, `fn*` tools, `vi`, `zm`, `tetris`, `info`, SN76489 audio, RTC).
   - **Fuzix**:
     - On `tfr911h`: `multicomp09` engine (128 KB RAM, 8 KB Multicomp09 MMU at `$FFDE/$FFDF`, 6850 ACIA at `$FFD0`, SD card at `$FFD2-$FFD7`).
     - On CoCo 2 + Centipede 32Z (`FUZIX_COCO2CENTIPEDE`): 256 KB banked RAM + CoCo 3 MMU (`$FF90/$FF91`, `$FFA0-$FFAF`) + DMA block disk (`$FF50-$FF58`) backed by LittleFS `/f0`.
   - **Hatvan OS**:
     - Experimental microkernel OS on `tfr911h` (`gep9` 256 KB 4-task 64 KB MMU with hardware-assisted cross-task `TFR`/`MOV` at `$FF91-$FF93`, and `gep9big` 2 MB 32-register MMU at `$FFA0-$FFBF`).
   - **Radio Shack BASIC**:
     - Color BASIC, Extended Color BASIC, Disk Extended Color BASIC (`disk11_rom.h`, `hdbchs_rom.h`, `hdbdw3cc3_rom.h`, `hdbsdc_rom.h`), and CoCo 3 Super Extended BASIC (`coco3_rom.h`), with Tether `~load`/`~save` live BASIC tokenizer/detokenizer and `centipede run-decb` preloader.

---

## 2. Deep-Dive Audit of `../v4/`: How and Why It Fragmented

In `../v4/`, the codebase split into three largely independent trees, each duplicating the RP2350B support libraries and the Go `tether` program:

1. **Root `tfr911h` tree**: `v4/tfr911h/` + `v4/firmware/one/` + `v4/tether/`
2. **Centipede 32Z tree**: `v4/centipede-32z/firmware/` + `v4/centipede-32z/three/` + `v4/centipede-32z/tether/` (plus `v4/centipede-32z/centiscope-firmware/`, which actually reaches back into `../../firmware/one`!)
3. **TurboLab tree**: `v4/turbolab/firmware/` + `v4/turbolab/tether/`

### 2.1 Comparison of `v4/firmware/one/` vs `v4/centipede-32z/three/`

`v4/firmware/one/` and `v4/centipede-32z/three/` are copy-pasted clones of the RP2350B Core 0 infrastructure:
- **`littlefs/`**, **`tcl6.7c/`**, and **`miniz/`** are **100% byte-for-byte identical** between `one/` and `three/`.
- **`util/`** is identical except for **`util/cobs.h`**, where `three/util/cobs.h` fixed a major bug in `one/util/cobs.h` (see Snag #4 below).
- **`firmware/`** has 25 identical files, 2 target-specific files (`coco3_rom.h`, `config.h`), and **9 diverged headers** where bug fixes or features were applied to one copy and never backported to the other:

| File | `v4/firmware/one/firmware/` (used by `tfr911h`) | `v4/centipede-32z/three/firmware/` (used by `centipede-32z`) | Recommendation for `v5/` |
| :--- | :--- | :--- | :--- |
| **`util/cobs.h`** | Copies payload + checksum into `unsigned char ckbuf[1024]` on stack; **silently truncates payloads > 1023 bytes**. | Zero-copy lambda `get_byte(idx)` with no stack buffer and no 1023-byte limit. | **Use `three`'s zero-copy `util/cobs.h`.** |
| **`vfs.h`** | **Chunks `TetherFile::read` and `write` into 256-byte RPC calls**; strips `/lfs` prefix in `LfsNode::lookup` so `/lfs/foo` works. | Sends unchunked `size` in a single RPC call (**breaks/truncates reads > 256 B!**); lacks `/lfs` alias. | **Use `one`'s chunked `vfs.h` + `/lfs` alias.** |
| **`vfs_rpc.h`** | Works both inside a coroutine (`coro_yield`) **and on the main stack** (`Coro::current() == nullptr` -> loops `PumpUsbCobs()`). Required by `tfr911h` during Phase 1->2 OS loading! | Asserts `CENTIPEDE_ASSERT(coro, "vfs_rpc: no Coro")` — **crashes if called outside a coroutine**. | **Use `one`'s dual-mode `vfs_rpc.h`.** |
| **`keyboard_injector.h`** | Old `volatile uint8_t row_response[8]` updated directly by Core 0; 128 max script actions. | **Double-buffered 256-entry `probe_table[2][256]`** + `active_table_idx` with `__dmb()` for single-cycle Core 1 `$FF00` lookup; 256 actions; `queue_char()`; incremental string consumption; CRLF deduplication. | **Use `three`'s double-buffered `keyboard_injector.h`.** |
| **`floppy.h`** | `ReadScsFloppy` / `WriteScsFloppy` not marked `IN_RAM`; `DontUseFloppy()` calls `cobs_printf` in flash. | Marks `ReadScsFloppy` & `WriteScsFloppy` `FORCE_INLINE static void IN_RAM` and removes `cobs_printf` from Core 1 path. | **Use `three`'s `IN_RAM` `floppy.h`.** |
| **`cobs_tx.h`** | Defines `C_SHUTDOWN = 255` and `cobs_shutdown(status)` so `tfr911h` can tell `tether` to exit or dump panic state. | Missing `C_SHUTDOWN` and `cobs_shutdown()`. | **Use `one`'s `cobs_tx.h`.** |
| **`pico_rpc.h`** | Only implements `ping`, `reboot`, `get-label`, `inject`. | Adds **`get-ram`**, **`put-ram`**, **`type`**, **`get-status`**, and **`get-cycles`**. | **Merge both** (`get-ram`/`put-ram` hook into the active engine's `Peek1`/`Poke1`/`PPeek1`/`PPoke1`). |
| **`rtc.h`** | Uses `static` / non-inline definitions for `g_sys_time`, `g_timer`, `start_20ms_timer`. | Marks globals and functions `inline` for clean multi-TU inclusion. | **Use `three`'s `inline` `rtc.h`**, plus add Andrew Diller's `T_WALLTIME` + `$FFC0-$FFC7` registers. |
| **`tcl_commands.h`** | Has `centipede type` (without `start_if_queued`). | Calls `keyboard_injector::start_if_queued()` and adds `centipede run-decb <filename>`. | **Use `three`'s `tcl_commands.h`**, plus add Andrew Diller's `fujinet`, `beep`, and `click` Tcl commands. |
| **`config.h` & `menu.h`** | Hardcodes `tfr911h` fields (`engine`, `flat_img`, `drive0..3`, `task1..2`). | Hardcodes `centipede-32z` fields (`become_coco3`, `fuzix`, `preload_decb`). | **Unify `Config` struct** with all fields (or board-specific extension fields) and table-driven Tcl variable binding in `menu.h`. |

### 2.2 Abandoned Unification Attempt in `v4/tfr911h/`

During our inspection of `v4/tfr911h/`, we found **12 header files** (`v4_types.h`, `v4_ram.h`, `v4_core_engine.h`, `v4_engine_tfr911.h`, `v4_engine_centipede.xxxxxh`, `v4_compat_centipede.h`, `v4_console.h`, `v4_floppy.h`, `v4_interrupts.h`, `v4_os_loader.h`, `v4_trace.h`, `v4_turbo9sim.h`) that are **dead code** — none of them are `#include`d by `v4_tfr911h_main.cpp`!
- Those 12 files were an earlier attempt to unify `tfr911h` and `centipede` using heavy C++ CRTP templates (`template <typename Derived, typename RamT, typename FloppyT> struct CoreEngine`).
- Why did that CRTP approach stall? Because the **inner PIO bus cycle loop on Core 1** is fundamentally different between:
  - `hamster.pio` on `tfr911h` (where RP2350B is the master clock generator, reads `A0..A15` from `sio_hw->gpio_hi_in`, and always drives `D0..D7` on reads), and
  - `gerbil.pio` on `centipede-32z` (where RP2350B is a slave to the CoCo's `EBAR`/`QBAR`, must decide within ~150 ns whether to `GERBIL_DRIVE(byte)` with `SLENB*` asserted or `GERBIL_PASS()` to let the CoCo motherboard respond and then sniff the resulting byte, and must also handle `SpoonfeedConsoleOnReset()`).
- In contrast, the **table-driven `IOReaders[256]` / `IOWriters[256]` dispatch** in `v4_tfr911h_main.cpp` (combined with `IN_RAM` peripheral handlers for `$FF00-$FFFF` and modular headers like `v4_gep9_mmu.h`, `v4_gep9_io.h`, `v4_multicomp09.h`, `floppy.h`, `keyboard_injector.h`, `orchestra90.h`) worked cleanly and stayed fast enough for 2+ MHz 6309 operation.
- **Lesson for `v5/`**: Share **Core 0 infrastructure** (LittleFS, Tcl 6.7c, VFS, COBS, PicoRPC, cross-core queues) and **composable `IN_RAM` I/O & MMU device modules** (ACIA, EmuDsk, Floppy, FujiNet Becker, Audio, RTC, Keyboard Injector, SAM/GIME/GEP9/Multicomp09 MMUs), while letting each board target (`tfr911h`, `centipede-32z`, `turbolab`) own its tight Core 1 PIO bus loop!

### 2.3 Comparison of the Three `tether` Implementations in `../v4/`

1. **`v4/tether/` vs `v4/centipede-32z/tether/`**:
   - 32 `.go` files (and `vendor/websocket-main/`) are 100% identical.
   - `v4/tether/` uniquely has `dump_panic.go` (`Dump6309PanicState` on `C_SHUTDOWN=255`), `C_CORE_DUMP=189`, `--reconnect`, `--tcl_input`, and Linux `/dev/ttyACM*` fallback globbing.
   - `v4/centipede-32z/tether/` uniquely has `bas_token.go` (Color/Extended/Disk/CoCo 3 BASIC tokenizer/detokenizer), `load_command.go` (`~load`), `save_command.go` (`~save`), `font5x7.go`, CoCo 3 GIME text & `HSCREEN` PNG rendering + live background `get-ram` screen polling in `samvdg.go`, 256 KB Fuzix MMU support in `ram_coco3.go`, `/pico_rpc` HTTP endpoints in `webserver.go`, and `--quick-get-ram`/`--quick-get-text`/`--quick-get-pmode`/`--quick-get-hscreen`/`--quick-get-cycles`/`--quick-type`.
2. **`v4/turbolab/tether/`**:
   - A clean, standalone Go CLI built for `turbolab/firmware`'s RPC handshake (`tuning`, `poke`, `watch`, `start`, `term_rx`) and 12-byte hardware cycle trace records (`C_TRACE_CYCLES = 200`), featuring `listings.go` (automatic OS-9 module scan and `.list` relocation), `os9_trace.go`, `watchpoint.go`, and `C_FAULT` register dump decoding.

---

## 3. Audit of Andrew Diller's Commits (`~/github.com/dillera/tfr9`)

We examined all 15 commits by Andrew Diller (`b009255..77ad30c` on `nitros9-l2-tfr911`, April 10–19, 2026).

### 3.1 Summary of What Andrew Built
1. **`v4/tfr911h/v4_nitros9_l2.h`** (`b009255`, `a9d0b1a`, `7c24e93`, `e6c0d4c`):
   - A standalone NitrOS-9 Level 2 engine (`l2::Foreground()`) with a 2 MB CoCo 3 MMU (`$FF90/$FF91`, `$FFA0-$FFAF`), M6850 ACIA at **`$FF14/$FF15`**, USB `C_DISK_READ`/`C_DISK_WRITE` block disk at **`$FF80-$FF86`**, hardware info port at **`$FF43/$FF44`**, `clock2_jvemu` binary RTC at **`$FFC0-$FFC7`**, and baked-in C array ROM headers (`nitros9_l2_rom.h`, `nitros9_l2fn_rom.h`).
2. **`v4/tfr911h/v4_audio.h`** (`7c24e93`):
   - Core 0 31.25 kHz hardware timer ISR (`alarm_num = 2`) driving PWM audio on `GPIO 8` (J3 pin 1 -> external PAM8302A amp), emulating a **TI SN76489 PSG** (3 square-wave tone channels + 1 LFSR noise channel written at `$FF41`) plus non-blocking console keyclicks (`$FF42` bit 0) and terminal bell (`$FF42` bit 1), and Tcl `beep` / `click` commands.
3. **`v4/tfr911h/v4_fujinet.h`** (`7e99f77`):
   - Core 0 `uart0` interrupt handler (`921600` baud 8N1 on `GPIO 12` TX / `GPIO 13` RX) with lock-free SPSC ring buffers (`rx_ring[4096]`, `tx_ring[1024]`), exposing a Becker-compatible status/data port to Core 1 (`$FF45` status bit 1 = RX ready, `$FF46` data read/write), plus a Tcl `fujinet` command (`fujinet`, `fujinet mount N`, `fujinet stats`) that speaks native FujiNet `0xE2` protocol from the boot prompt before NitrOS-9 even boots.
4. **NitrOS-9 Level 2 & FujiNet Software Suite (`v4/nitros9-l2/`)** (`b009255..77ad30c`):
   - **Kernel Driver Fix (`scdwv.asm` + `dwio.asm`)**: Solved the fatal contention between NitrOS-9's background DriveWire polling (`OP_SERREAD` every 1/60s tick) and multi-byte FujiNet `0xE2` commands by:
     1. Adding **`SS.Fuji = $E2`** (`SetStat`) to `scdwv.asm`, which masks interrupts (`orcc #IntMasks`), sends `[0xE2, cmd, a1..a4]` (and optional write payload) via `DW$Write`, waits for the response via `DW$Read` (`bcs`), and copies the reply back into the caller's buffer using `F$LDABX`/`F$STABY` across task spaces.
     2. Building `dwio_fujinet.sb` with `DWIO_NO_VIRQ = 1` and `BECKER = 1` so DriveWire never polls `OP_SERREAD` in the background on `/N1`.
   - **Ported `fujinet-lib` v5.0.1 (`bus/os9/ssfuji.c`)** + 11 assembly CLI tools (`fnconfig`, `fnstatus`, `fnlisthosts`, `fngethost`, `fnsethost`, `fnmount`, `fnlistdevs`, `fngetdevfile`, `fnsetdevfile`, `fnmountimg`, `fnwx`) + 5 CMOC C network apps (`fnweather`, `fnnews`, `fnstocks`, `fnbounce`, `fnbattleship`).
   - **Userland utilities & games**: `vi` (47 KB screen editor with raw arrow-key support via `$FF42` bit 7), `zm` (Z-machine interpreter + Zork I/II/III), `tetris`, `info`, `beep`, `click`, `clear`, `unix.tcl` aliases, and a 26-page LaTeX manual (`doc/manual.tex`).
5. **Tether Fixes (`v4/tether/disk.go`, `tconsole.go`)** (`b009255`, `a9d0b1a`, `e6c0d4c`):
   - Fixed a critical variable-shadowing panic bug in `EmulateDiskWrite` (`disk.go`).
   - Fixed UTF-8 multi-byte character corruption in `tconsole.go`.
   - Added `-pass_ctrl_c` and `T_WALLTIME = 183` wall-clock sync packet in `tconsole.go`.

### 3.2 What Is Redundant vs. What to Adopt in `v5/`

- **Redundant (Do NOT copy `v4_nitros9_l2.h`'s engine loop or baked-in ROM arrays)**:
  - `v4_nitros9_l2.h`'s `l2::Foreground()`, `l2::Phys()`, and raw `C_DISK_READ`/`C_DISK_WRITE` `ServiceDisk()` duplicate `v4/tfr911h`'s existing **`ENGINE_DEEP65280`** (`setup_deep65280()`, `translate_deep_addr()`, and VFS-backed `FG2BG_EMUDSK`).
  - Embedding 64 KB kernel images as C arrays in flash (`nitros9_l2_rom.h`, `nitros9_l2fn_rom.h`) wastes flash and requires recompiling RP2350 firmware whenever a NitrOS-9 module changes; `v4`'s VFS loading (`/pc/deep65280.img` or `/lfs/deep65280.img`) is much cleaner.
- **High-Value Components to Adopt and Unify into `v5/`**:
  1. **`v4_fujinet.h`** (generalized so its Becker port address is configurable and usable by both `tfr911h` and `centipede-32z`).
  2. **`v4_audio.h`** (generalized with board-specific PWM pin `BOARD_AUDIO_PIN` and unified with `orchestra90.h`).
  3. **Hardware Info Port (`$FF43/$FF44`), JV-Emu RTC (`$FFC0-$FFC7` + `T_WALLTIME`), and Raw/Translated Arrow Keys (`$FF42` bit 7)** integrated into the shared I/O device library.
  4. **Tether fixes** (`disk.go` `EmulateDiskWrite` fix, UTF-8 output fix, `-pass_ctrl_c`, `T_WALLTIME`).
  5. **The `nitros9-l2/` FujiNet drivers, libraries, apps, and userland recipes** (`scdwv.asm`, `dwio.asm`, `fujinet-lib`, `fn*` commands, `vi`, `zm`, `tetris`, `info`, `beep`, `click`, `manual.tex`).

---

## 4. Snags for the Unwary (Critical Traps & Bugs Discovered)

Anyone working on `v5/` should read this section carefully. Every item below is a real bug or hardware/software trap verified in `../v4/`, `~/github.com/dillera/tfr9`, or the PCB files.

### Snag #1: Missing `#define PICO_RP2350A 0` in `tfr911h_rp2350b.h` Breaks GPIO Count and Chip Temperature ADC!
- **Where**: `v4/tfr911h/boards/tfr911h_rp2350b.h` and `v4/turbolab/firmware/boards/tfr911h_rp2350b.h`.
- **The Trap**: Both board headers only contain `#define PICO_DEFAULT_LED_PIN 25` and **omit `#define PICO_RP2350A 0`**! Even though `CMakeLists.txt` sets `set(PICO_RP2350A 0)`, the Pico SDK's `cmsis_rename_exceptions.h` / `pico/config_autogen.h` defaults the C preprocessor macro `PICO_RP2350A` to `1` unless the board header explicitly defines `#define PICO_RP2350A 0` (as `solderparty_rp2350_stamp_xl.h` does).
- **Consequences**:
  1. `NUM_BANK0_GPIOS` evaluates to `30` (RP2350A) instead of `48` (RP2350B), causing SDK bounds checks (`assert(gpio < NUM_BANK0_GPIOS)`) to fail on GPIOs 30..47!
  2. In `hardware/adc.h`, `ADC_TEMPERATURE_CHANNEL_NUM` evaluates to **`4`** (RP2350A) instead of **`8`** (RP2350B)! On the RP2350B (QFN-80), ADC channel 4 is **external pin `GPIO 44` (`A12` on the 6809 address bus!)**, while the internal temperature diode is on ADC channel 8! That is why Andrew Diller's `ChipTemperature()` in `v4_nitros9_l2.h` read wild voltages (`-200C` to `+400C`) from address line `A12`!
- **Fix in `v5/`**: Always include `#define PICO_RP2350A 0` in `tfr911h_rp2350b.h` and use `ADC_TEMPERATURE_CHANNEL_NUM` (which will then correctly be `8`).

### Snag #2: `hat-fuji-40c` vs `40c-bodged` vs `hat-fuji-41d` Pinout Discrepancy (`J3-15` `IO21` vs `J2-15` `IO13` and `IO32` vs `IO33`)
- **Where**: `hat-fuji-40c/README.md` vs `hat-fuji-41a.kicad_pcb` / `hat-fuji-41d.kicad_pcb` vs `dillera/tfr9/v4/nitros9-l2/fujinet/README.md`.
- **The Trap**:
  - In `hat-fuji-40c`, the UART lines were originally routed to `J1-1` (`GPIO 8`) and `J1-3` (`GPIO 9`), which **collided with `G_CTS` (`GPIO 8`) and `G_SCS` (`GPIO 9`) on `centipede-32z`**.
  - Bill Athing's bodge in `hat-fuji-40c/README.md` removed `R1`/`R2` and wired:
    - `J1-9` (RP2350 `GPIO 12`) -> `J2-8` (ESP32 **`IO32`**)
    - `J1-11` (RP2350 `GPIO 13`) -> **`J3-15`** (ESP32 **`IO21`**, which is `PIN_UART1_TX` in `coco_devkitc.h`)
    - `J1-13` (RP2350 `GPIO 14`) -> `J2-10` (ESP32 **`IO25`**)
    - `J1-15` (RP2350 `GPIO 15`) -> `J2-9` (ESP32 **`IO33`**)
  - However, when `hat-fuji-41a` and `hat-fuji-41d` were laid out in KiCad (`hat-fuji-41d.kicad_pcb`), `J1-11` (`/H13` = RP2350 `GPIO 13`) was routed to **`J2-15` (ESP32 `IO13`)** instead of **`J3-15` (ESP32 `IO21`)**!
  - Furthermore, Andrew Diller's custom-wired hat used ESP32 `PIN_UART1_RX=33` (`J2-9` = `J1-15` = `GPIO 15` on `41d`) and `PIN_UART1_TX=21` (`J3-15` on `40c-bodged`).
- **Complete Pin Matrix Across All Three Hat Variants**:

| 2x10 Header Pin (`J3` on board / `J1` on hat) | RP2350B GPIO (Identical on `tfr911h` & `centipede-32z`) | RP2350B Hardware Peripherals | `hat-fuji-40c` (unbodged — DO NOT USE on Centipede) | `hat-fuji-40c` (Bill Athing Bodge) | `hat-fuji-41a` & `hat-fuji-41d` PCB |
| :---: | :---: | :--- | :--- | :--- | :--- |
| **Pin 1** | `GPIO 8` (`G_CTS` on Centipede; Audio on `tfr911h`) | UART1_TX / PWM4A | `R2` -> `J3-15` (ESP32 `IO21`) | Disconnected (`R2` removed) | Disconnected |
| **Pin 3** | `GPIO 9` (`G_SCS` on Centipede) | UART1_RX / PWM4B | `R1` -> `J2-8` (ESP32 `IO32`) | Disconnected (`R1` removed) | Disconnected |
| **Pin 5** | `GPIO 10` | UART1_CTS / SPI1_SCK | `R5` -> `J3-18` (ESP32 `IO22` `PIN_CD`) | `R5` -> `J3-18` (ESP32 `IO22`) | `R5` -> `J3-18` (ESP32 `IO22`) |
| **Pin 9** | **`GPIO 12`** | **`UART0_TX`** / `SPI1_RX` | Unconnected | `J2-8` (**ESP32 `IO32`**) | `J2-8` (**ESP32 `IO32`**) |
| **Pin 11** | **`GPIO 13`** | **`UART0_RX`** / `SPI1_CSn` | Unconnected | `J3-15` (**ESP32 `IO21`**) | `J2-15` (**ESP32 `IO13`**) |
| **Pin 13** | **`GPIO 14`** | **`UART0_CTS`** / `SPI1_SCK` | Unconnected | `J2-10` (**ESP32 `IO25`**) | `J2-10` (**ESP32 `IO25`**) |
| **Pin 15** | **`GPIO 15`** | **`UART0_RTS`** / `SPI1_TX` | Unconnected | `J2-9` (**ESP32 `IO33`**) | `J2-9` (**ESP32 `IO33`**) |

- **Fix in `v5/`**:
  - On the RP2350B side, `GPIO 12` (`UART0_TX`) and `GPIO 13` (`UART0_RX`) are always the hardware `uart0` pins on both `tfr911h` and `centipede-32z`.
  - On the ESP32 `fujinet-firmware` side:
    - For **`hat-fuji-41d`** (unmodified PCB): configure ESP32 UART1 with **`PIN_UART1_RX = GPIO_NUM_32`** (receiving from RP2350 `GPIO 12` on `J2-8`) and **`PIN_UART1_TX = GPIO_NUM_13`** (transmitting to RP2350 `GPIO 13` on `J2-15`).
    - For **`hat-fuji-40c` (Bill Athing bodge)**: configure ESP32 UART1 with **`PIN_UART1_RX = GPIO_NUM_32`** (`J2-8`) and **`PIN_UART1_TX = GPIO_NUM_21`** (`J3-15`).

### Snag #3: Audio PWM Pin Collision (`GPIO 8` on `tfr911h` vs `G_CTS` on `centipede-32z`)
- **Where**: `dillera/tfr9/v4/tfr911h/v4_audio.h` (`AUDIO_PIN = 8`) vs `v4/centipede-32z/firmware/centipede.cpp` (`G_CTS = 8`, `G_SND = 26`).
- **The Trap**: On `tfr911h`, `GPIO 8` goes only to expansion header `J3` pin 1, where Andrew connected an external PAM8302A audio amplifier. On `centipede-32z`, **`GPIO 8` is `G_CTS` (the CoCo's `$C000-$FEFF` Cartridge Select output)**, while the CoCo cartridge analog sound input (`G_SND`) is on **`GPIO 26`** (used by `orchestra90.h`)! Driving PWM on `GPIO 8` on a Centipede 32Z will fight the CoCo's 74LS138 address decoder and crash the CoCo!
- **Fix in `v5/`**: Define `BOARD_AUDIO_PIN` per board (`8` on `tfr911h`, `26` on `centipede-32z`) and never hardcode GPIO numbers inside shared audio headers.

### Snag #4: Unchunked VFS Reads (`three/firmware/vfs.h`), Non-Coroutine VFS Assert (`three/firmware/vfs_rpc.h`), and 1 KB Stack Truncation (`one/util/cobs.h`)
- **Where**: `v4/centipede-32z/three/firmware/vfs.h`, `vfs_rpc.h`, and `v4/firmware/one/util/cobs.h`.
- **The Traps**:
  1. `three/firmware/vfs.h` passes `size` directly to a single `vfs_rpc_call("read", ...)` without chunking into 256-byte blocks. Because the RPC buffer is 512 bytes, reading > 256 bytes from `/pc/...` via `Vfs::read()` silently truncates or fails unless the caller manually loops in 256-byte chunks!
  2. `three/firmware/vfs_rpc.h` calls `CENTIPEDE_ASSERT(coro, "vfs_rpc: no Coro")`. If `tfr911h` used `three`'s `vfs_rpc.h`, it would crash immediately after `bye` when `setup_flat65280()` / `setup_deep65280()` loads `/pc/deep65280.img` from the main stack (`g_vfs_coro == nullptr`).
  3. `one/util/cobs.h` allocates `unsigned char ckbuf[1024]` on the stack and clamps `if (len > 1023) len = 1023;`, corrupting any COBS packet > 1023 bytes and risking coroutine stack overflow.
- **Fix in `v5/`**: Combine `one`'s chunked `vfs.h` and dual-mode `vfs_rpc.h` with `three`'s zero-copy `util/cobs.h`.

### Snag #5: Variable Shadowing Bug in `v4/tether/disk.go` (`EmulateDiskWrite` Panics on Every Write!)
- **Where**: `v4/tether/disk.go` (and `v4/centipede-32z/tether/disk.go`) in `EmulateDiskWrite()`.
- **The Trap**: Inside the `case 4:` block that saves the 4-byte LSN parameter (`disk_param[i] = b`), `v4/tether/disk.go` accidentally redeclared `var disk_param [4]byte` inside the `if n == 4` scope, shadowing the package-level `disk_param`! When the 256-byte sector payload arrives in `case 256:`, the package-level `disk_param` is still `[0, 0, 0, 0]` (`drive=0, lsn=0, size=0`), which triggers `log.Panicf("bad disk_param size indicator: %x", ...)` on the very first `C_DISK_WRITE`!
- **Fix in `v5/`**: Apply Andrew Diller's 4-line fix from commit `b009255` (removing the inner `var disk_param [4]byte`).

### Snag #6: Core 1 Flash XIP Contention & Compiler Switch-Jump-Table Trap
- **Where**: Any function called from Core 1's real-time bus loop (`hamster.pio` or `gerbil.pio`).
- **The Trap**:
  - Core 1 has ~150–250 ns per 6809/6309 bus cycle to decode the address, translate MMU banks, and push the read byte to the PIO TX FIFO.
  - If Core 1 executes even a single instruction from QSPI XIP Flash (or reads a `switch` jump table or `const` lookup table that GCC placed in `.rodata` in Flash!) while Core 0 is erasing/programming LittleFS or suffering an XIP cache miss, Core 1 stalls for milliseconds and misses the 6809 bus cycle, crashing the 6809 CPU!
  - We caught `one/firmware/floppy.h` missing `IN_RAM` on `ReadScsFloppy`/`WriteScsFloppy` and calling `cobs_printf` in `DontUseFloppy()`.
  - Furthermore, in C++, large `switch` statements inside `__not_in_flash_func` functions often cause `arm-none-eabi-gcc` to emit a jump table in `.rodata` (Flash) unless `if/else` chains or explicit `__not_in_flash("mydata")` dispatch tables (`IOReaders[256]`, `IOWriters[256]`) are used!
- **Fix in `v5/`**:
  - Every function and lookup table touched by Core 1 must be marked `IN_RAM` (`__not_in_flash("ram_text")`) or `FORCE_INLINE`.
  - Add a build-time verification target (`check-core1-sram` using `arm-none-eabi-nm` / `objdump`) that verifies no Core 1 symbol or `.rodata` table referenced by Core 1 resides in `0x10000000..0x1FFFFFFF` (XIP Flash).

### Snag #7: I/O Address Map Conflicts Between `deep65280`, Andrew Diller's `l2`, and `centipede-32z` (`$FF14/$FF15` vs `$FF88/$FF89`, and `$FF40-$FF4F`)
- **Where**: `v4/tfr911h/v4_tfr911h_main.cpp` vs `dillera/tfr9/v4/tfr911h/v4_nitros9_l2.h` vs `v4/centipede-32z/firmware/centipede.cpp`.
- **The Traps**:
  1. **Console ACIA Address (`$FF88/$FF89` vs `$FF14/$FF15`)**:
     - `v4/tfr911h`'s `flat65280` and `deep65280` engines map the 6850 ACIA console to **`$FF88` (status) / `$FF89` (data)**.
     - Andrew Diller's `v4_nitros9_l2.h` mapped the 6850 ACIA console to **`$FF14` (status) / `$FF15` (data)**, and his `nitros9-l2/fujinet/c/term.c` helper writes directly to **`$FF15` (`sta $FF15`) from user mode** to bypass OS-9 SCF!
     - On a real CoCo (and `centipede-32z`), `$FF14/$FF15` is a mirror of **PIA 0 (`$FF00-$FF1F`)**!
  2. **`$FF40-$FF4F` (CoCo `SCS*` Floppy Controller vs Audio/Info/Becker Ports)**:
     - On a CoCo with a floppy controller (or `centipede-32z` floppy emulation in `floppy.h`), `$FF40-$FF4B` is decoded by `SCS*` for the WD1773/1793 disk controller.
     - Standard CoCo DriveWire/FujiNet Becker port is at **`$FF41` (status) / `$FF42` (data)**.
     - Andrew Diller placed SN76489 PSG at **`$FF41`**, Audio/Keyboard Control at **`$FF42`**, Hardware Info Port at **`$FF43/$FF44`**, and moved the FujiNet Becker port to **`$FF45` (status) / `$FF46` (data)**.
     - On a CoCo (`centipede-32z`), `$FF41..$FF46` are unused registers within the `$FF40-$FF4F` `SCS*` page (since the CoCo Disk Controller only uses `$FF40` control latch and `$FF48-$FF4B` WD1793 registers)! Thus `$FF41..$FF46` **do not collide with `$FF40` or `$FF48-$FF4B`**, provided `centipede-32z`'s `SCS*` handler dispatches `$FF41..$FF46` to Audio/Info/FujiNet and `$FF40, $FF48-$FF4B` to `floppy.h`!
- **Fix in `v5/`**:
  - In `tfr911h`'s unified `deep65280` engine, map the 6850 ACIA at **both `$FF88/$FF89` and `$FF14/$FF15`** (so both `v4` `deep65280` images and Andrew's `nitros9-l2` images + `term.c` work unmodified!).
  - Fix `term.c` to use standard OS-9 `I$WritLn` on path 1 (with raw mode enabled via `SS.Opt`) instead of hardcoded `sta $FF15` so all `fn*` C apps also work on a real CoCo via Centipede!
  - Support Becker port at **`$FF45/$FF46`** (and optionally `$FF41/$FF42` when SN76489 audio is not occupying `$FF41/$FF42`) on both `tfr911h` and `centipede-32z` (inside the `$FF40-$FF4F` `SCS*` region alongside `$FF40/$FF48-$FF4B` floppy emulation).

### Snag #8: ESP32 DevKitC `GPIO 14` (`SAFE_RESET` / `SW3`) Capacitive Crosstalk Resets FujiNet
- **Where**: `hat-fuji-40c`/`41a`/`41d` (`SW3` on `/IO14/SAFE_RESET`, `R7` unpopulated or weak) + `fujinet-firmware` (`PIN_BUTTON_C = GPIO_NUM_14`).
- **The Trap**: In `fujinet-firmware` (`lib/hardware/fnHardwareTimer.cpp` / `buttons.cpp`), `PIN_BUTTON_C` (`GPIO 14`) triggers an immediate ESP32 reboot when pulled low. If `R7` is not populated on the hat and the ESP32 internal pull-up is not enabled, 921,600-baud bursts on the adjacent traces (`J2-9`, `J2-10`, `J2-15`) couple into `J2-12` (`IO14`) and randomly reboot the ESP32 mid-transfer!
- **Fix in `v5/`**: Document that `fujinet-firmware` builds for `hat-fuji-41d` must set `-DPIN_BUTTON_C=GPIO_NUM_NC` (or enable internal `GPIO_PULLUP_ONLY` on `GPIO 14`).

### Snag #9: CMOC + `lwlink --format=os9` Toolchain Traps for NitrOS-9 Level 2 C Programs
- **Where**: Building C programs (like `fnweather`, `fnnews`, `fnstocks`, `fnbounce`, `fnbattleship`) with CMOC for NitrOS-9 Level 2.
- **The Traps** (discovered by Andrew Diller):
  1. **Unrelocated `const char*` Tables in Global Struct Arrays**: CMOC compiles `static const struct { const char *a; } tbl[] = { {"foo"} };` into the `initgl` section using absolute link-time addresses without emitting OS-9 data-text relocation entries. Under NitrOS-9 Level 2, where the module is loaded at an arbitrary address, those pointers point into garbage memory and crash! **Workaround**: Use fixed-size `char a[16]` arrays inside structs or assign pointers inside a function at runtime.
  2. **256-Byte Stack Default**: `lwlink --format=os9` defaults the module header permanent storage + stack allocation to **256 bytes** (`$0100`). Any C program with local buffers will corrupt its own globals unless linked with `-Wl,--stack=2048` (or `4096`).
  3. **CMOC `sprintf` Lacks Precision Specifiers (`%.2s`)**: Using `%.2s` prints literal `.2s` instead of truncating the string.

### Snag #10: `centiscope-firmware/gerbil.pio` vs `centiscope.cpp` Mismatch
- **Where**: `v4/centipede-32z/centiscope-firmware/gerbil.pio` vs `centiscope.cpp`.
- **The Trap**: `centiscope-firmware/gerbil.pio` defines `.program passive_gerbil`, which pushes only 1 word per bus cycle and never pulls from the TX FIFO. However, `centiscope.cpp` calls `gerbil_program_init()`, `GERBIL_PASS()`, and `GERBIL_GET()` twice per cycle (expecting the standard `gerbil.pio` program!).
- **Fix in `v5/`**: Share a single `gerbil.pio` across `centipede-32z` and `centiscope`.

---

## 5. Proposed Unified `v5/` Directory Structure

We propose the following clean, self-contained hierarchy for `/home/strick/modoc/coco-shelf/tfr9/v5/`. Nothing is symlinked or included from `../v4/`; all code lives inside `v5/` with zero duplication of `littlefs`, `tcl6.7c`, `miniz`, `vfs`, `cobs`, `pico_rpc`, I/O emulators, or `tether`.

```text
v5/
├── Makefile                        # Top-level build, test, flash, and OS recipe runner
├── CMakeLists.txt                  # Top-level CMake project building all RP2350B UF2 firmwares
├── pico_sdk_import.cmake
├── doc/
│   ├── v5-notes-and-recommendations.md
│   ├── hardware-pinouts.md         # Complete J3 / hat-fuji-41d / tfr911h / centipede-32z pin tables
│   ├── memory-and-io-maps.md       # Unified 6809/6309 I/O & MMU register reference across all engines
│   └── manual.tex                  # NitrOS-9 Level 2 & FujiNet manual (from dillera/tfr9)
│
├── rp2350/                         # SHARED RP2350B C/C++ LIBRARIES & INFRASTRUCTURE
│   ├── CMakeLists.txt              # Defines static libs: v5_littlefs, v5_tcl, v5_miniz, v5_core0, v5_io_emu
│   ├── boards/                     # Pico SDK board headers (ALL define PICO_RP2350A=0!)
│   │   ├── tfr911h_rp2350b.h       # Fixed: #define PICO_RP2350A 0, BOARD_AUDIO_PIN 8, PSRAM CS 19
│   │   └── centipede_32z_rp2350b.h # #define PICO_RP2350A 0, BOARD_AUDIO_PIN 26, G_CTS=8, G_SCS=9
│   ├── third_party/
│   │   ├── littlefs/               # Single copy of LittleFS (from v4/firmware/one/littlefs)
│   │   ├── tcl6.7c/                # Single copy of Tcl 6.7c (from v4/firmware/one/tcl6.7c)
│   │   └── miniz/                  # Single copy of miniz (from v4/firmware/one/miniz)
│   ├── util/
│   │   ├── circbuf.h               # Lock-free SPSC ring buffer
│   │   ├── cobs.h                  # Zero-copy COBS encoder/decoder (from three/util/cobs.h)
│   │   ├── font5x7.h               # 5x7 bitmap font (used by gspoon PMODE4 console)
│   │   └── tests/                  # Host unit tests (_fut_circbuf_test.cpp, _fut_cobs_test.cpp)
│   ├── core0/                      # Shared Core 0 Background Services (USB, VFS, Tcl, RPC, Flash)
│   │   ├── abort.h
│   │   ├── bug.h
│   │   ├── cobs_tx.h               # Includes C_SHUTDOWN=255, C_FAULT=188, C_CORE_DUMP=189, C_TRACE_CYCLES=200
│   │   ├── compress.h
│   │   ├── config.h                # Unified Config struct for tfr911h, centipede-32z, and turbolab
│   │   ├── console.h
│   │   ├── coro.h                  # Cooperative coroutines for Core 0 tasks
│   │   ├── cross_core.h            # SPSC cross-core queues (fg2bg, bg2fg)
│   │   ├── editor.h
│   │   ├── flash_label.h
│   │   ├── heuristic_file.h
│   │   ├── littlefs_mount.h        # LittleFS flash block device & mount logic
│   │   ├── md5.h
│   │   ├── menu.h                  # Unified boot menu + Tcl variable sync
│   │   ├── pcb.h                   # Packet Control Bytes (shared protocol constants with tether)
│   │   ├── pico_rpc.h              # Full RPC suite: ping, reboot, get-label, inject, get-ram, put-ram,
│   │   │                           #   type, get-status, get-cycles, tuning, poke, watch, start
│   │   ├── restart.h
│   │   ├── tcl_commands.h          # Unified Tcl commands: vfs, lfs, centipede, run-decb, fujinet, beep, click
│   │   ├── tcl_io.h
│   │   ├── usb_pipeline.h
│   │   ├── vfs.h                   # Chunked (256B) TetherFsNode + LfsNode (with /lfs alias)
│   │   ├── vfs_rpc.h               # Works both inside a Coro AND on main stack (Coro::current()==nullptr)
│   │   └── vfs_rpc.cpp
│   ├── pio/                        # Shared PIO State Machine Programs & Dynamic Assembler
│   │   ├── hamster.pio             # Master E/Q clock generator for tfr911h & turbolab
│   │   ├── pio_assembler.h         # Runtime tunable PIO assembler (from turbolab/firmware)
│   │   ├── hamster_dynamic.h       # Dynamic tunable hamster generator (MHZ, K1..K4, T1..T5)
│   │   └── gerbil.pio              # Slave EBAR/QBAR + SLENB* bus transceiver for centipede-32z & centiscope
│   ├── roms/                       # Embedded ROM images (compiled in only when referenced)
│   │   ├── turbo9os_rom.h
│   │   ├── disk11_rom.h
│   │   ├── hdbchs_rom.h
│   │   ├── hdbdw3cc3_rom.h
│   │   ├── hdbsdc_rom.h
│   │   └── coco3_rom.h
│   └── io_emu/                     # SHARED 6809/6309 I/O & MMU EMULATION (All Core 1 paths marked IN_RAM!)
│       ├── io_dispatch.h           # 256-entry IN_RAM IOReaders[256] / IOWriters[256] table + registration API
│       ├── acia_6850.h             # M6850 ACIA console ($FF88/$FF89, $FF14/$FF15, $FFD0/$FFD1) + arrow key xlat
│       ├── turbo9_uart.h           # TurboOS / Turbo9Sim console ($FF00/$FF01) + clock/stop ($FF87)
│       ├── emudsk.h                # TFR911H virtual block disk ($FF80-$FF86) via VFS / Tether
│       ├── floppy_wd1793.h         # CoCo WD1773/1793 + $FF40 latch emulation (IN_RAM, from three/floppy.h)
│       ├── sd_multicomp09.h        # Multicomp09 SD card controller ($FFD2-$FFD7)
│       ├── fuzix_coco_disk.h       # CoCo 2 Centipede Fuzix DMA disk ($FF50-$FF58)
│       ├── fujinet_becker.h        # UART0 (GPIO 12/13, 921600 baud) + Core 1 Becker port ($FF45/$FF46 or $FF41/$FF42)
│       ├── audio_sn76489_pwm.h     # Core 0 31.25kHz ISR + SN76489 ($FF41) + click/bell ($FF42) + Orchestra-90 ($FF7A/$FF7B)
│       ├── rtc_jvemu.h             # 20ms/60Hz timer + JV-Emu RTC ($FFC0-$FFC7) synced via T_WALLTIME
│       ├── hw_info_port.h          # $FF43/$FF44 streaming hardware telemetry (fixed RP2350B ADC ch 8 temp!)
│       ├── keyboard_injector.h     # Double-buffered 256-entry probe_table[2][256] ($FF00/$FF02)
│       ├── gspoon_console.h        # On-CoCo PMODE 4 spoonfed console (JMP $7E7E + GPoke1/GPeek1)
│       ├── trace_and_fault.h       # TurboLab watchpoints, Red-Page/Zero-Vector checks, SWI register harvester
│       └── mmu/
│           ├── mmu_flat64k.h       # 64 KB flat RAM
│           ├── mmu_coco_sam.h      # CoCo 1/2 SAM ($FFC0-$FFDF) + 64K RAM/ROM overlay
│           ├── mmu_coco3_gime.h    # CoCo 3 / deep65280 MMU ($FF90/$FF91, $FFA0-$FFAF; 128K, 256K, or 2MB)
│           ├── mmu_multicomp09.h   # Multicomp09 128 KB 8 KB bank MMU ($FFDE/$FFDF)
│           └── mmu_gep9.h          # Hatvan GEP9 4-task 64K MMU ($FF91-$FF93 TFR hook) & GEP9Big ($FFA0-$FFBF)
│
├── firmware/                       # BOARD / TARGET FIRMWARE ENTRY POINTS (thin composition layers)
│   ├── tfr911h/                    # Unified TFR911H firmware (engines: turbo9os, flat65280, deep65280,
│   │   ├── CMakeLists.txt          #   gep9, gep9big, multicomp09, and turbolab mode!)
│   │   └── main.cpp
│   ├── centipede-32z/              # Unified Centipede 32Z firmware (CoCo 1/2 64K, CoCo 3 128K,
│   │   ├── CMakeLists.txt          #   Fuzix 256K, DECB preload, Floppy, HDB-DOS, FujiNet hat!)
│   │   └── main.cpp
│   ├── centiscope/                 # Passive CoCo bus logic analyzer firmware for Centipede 32Z
│   │   ├── CMakeLists.txt
│   │   └── main.cpp
│   └── turbolab/                   # Dedicated student/automated-grading firmware for TFR911H
│       ├── CMakeLists.txt
│       └── main.cpp
│
├── tether/                         # SINGLE UNIFIED GO MODULE & CLI (`tether`)
│   ├── go.mod
│   ├── cmd/
│   │   ├── tether/                 # Unified tether binary supporting TFR911H, Centipede 32Z, and TurboLab!
│   │   │   └── main.go
│   │   └── make-uf2-label/
│   │       └── main.go
│   └── internal/                   # Cleanly factored packages (or top-level files):
│       ├── cobs/                   # COBS framing & checksum
│       ├── serial/                 # Cross-platform serial port + Linux /dev/ttyACM* auto-discovery
│       ├── rpc/                    # PicoRPC client + --quick-get-ram/text/pmode/hscreen/cycles/type
│       ├── basic/                  # Color/Extended/Disk/CoCo 3 BASIC tokenizer/detokenizer (~load, ~save)
│       ├── video/                  # VDG text, PMODE 0..4, CoCo 3 GIME 40/80-col text & HSCREEN 1..4 PNG + web UI
│       ├── disk/                   # EmulateDiskRead/Write (with EmulateDiskWrite shadowing bug fixed!)
│       ├── trace/                  # Cycle decompression, 6809/6309 disassembler, relocated .list, OS-9 SWI2 tracer
│       ├── fault/                  # Dump6309PanicState + TurboLab SWI register frame & /tmp/fault.img dumper
│       └── turbolab/               # TurboLab session runner (--watch, --trigger, --stop, --tuning, module loader)
│
├── os/                             # OS RECIPES, BOOT SCRIPTS, DRIVERS & USERLAND APPS
│   ├── tcl/                        # Shared Tcl boot scripts (pc/ and basefs/rc/ mode*.tcl, unix.tcl)
│   ├── turboos/                    # TurboOS images & student exercises
│   ├── nitros9-l1/                 # NitrOS-9 Level 1 recipes (flat65280, CoCo 2 floppy/HDB-DOS)
│   ├── nitros9-l2/                 # NitrOS-9 Level 2 recipes (deep65280, CoCo 3) + Andrew Diller's suite:
│   │   ├── fujinet/                #   scdwv.asm (SS.Fuji=$E2), dwio.asm, fujinet-lib, fn* asm & C apps
│   │   └── cmds/                   #   vi, zm (Zork), tetris, info, beep, click, clear
│   ├── fuzix/                      # Fuzix images & recipes (multicomp09 128K & CoCo 2 Centipede 256K)
│   ├── hatvan/                     # Hatvan OS microkernel recipes (gep9 & gep9big)
│   └── basic/                      # Sample Color/Extended/Disk/CoCo 3 BASIC programs
│
└── systests/                       # Automated Hardware-in-the-Loop & Host Regression Tests
    ├── unit/                       # Host C++ (util/tests) and Go (go test ./...) unit tests
    ├── tfr911h/                    # Automated tests for turbo9os, nitros9-l1, nitros9-l2, fuzix, hatvan
    ├── centipede-32z/              # Automated tests for CoCo 2 BASIC, CoCo 3, Fuzix, ~load/~save
    └── turbolab/                   # Automated tests for watchpoints, cycle traces, SWI fault dumps
```

---

## 6. Architectural Design of Key Shared Subsystems in `v5/`

### 6.1 Composable 6809/6309 I/O Emulation (`rp2350/io_emu/`)

On both `tfr911h` and `centipede-32z`, 6809/6309 memory-mapped I/O lives in the top page `$FF00-$FFFF` (256 bytes).
In `v5/`, we standardize on a zero-overhead `IN_RAM` dispatch table for `$FF00-$FFFF`:

```cpp
// rp2350/io_emu/io_dispatch.h
using IOReadFn  = uint8_t (*)(uint8_t offset);
using IOWriteFn = void    (*)(uint8_t offset, uint8_t val);

extern IOReadFn  IOReaders[256] IN_RAM;
extern IOWriteFn IOWriters[256] IN_RAM;

// For Centipede 32Z, a device can also indicate whether it overrides the CoCo bus
// (GERBIL_DRIVE with SLENB* asserted) or passively observes CoCo motherboard writes (GERBIL_PASS):
extern uint32_t  IODriveMask[8] IN_RAM; // 256-bit bitmap: 1 = drive bus on read, 0 = pass
```

Each peripheral in `rp2350/io_emu/` is a self-contained header with `IN_RAM` read/write handlers and an `Install_*()` registration function:

| Module (`rp2350/io_emu/`) | Registers | Used By | Notes |
| :--- | :--- | :--- | :--- |
| **`acia_6850.h`** | `$FF88/$FF89`, `$FF14/$FF15`, or `$FFD0/$FFD1` | `flat65280`, `deep65280`, `gep9`, `multicomp09` | Includes Andrew Diller's raw/translated arrow-key mode (`$FF42` bit 7). Registering at both `$FF88/$FF89` and `$FF14/$FF15` in `deep65280` ensures 100% compatibility with both `v4` and `dillera` images! |
| **`turbo9_uart.h`** | `$FF00/$FF01`, `$FF87` (`CLOCK_AND_STOP`) | `turbo9os`, `turbolab` | Polling console + cycle counter / shutdown / panic trigger (`$FE` -> `C_SHUTDOWN=255`). |
| **`emudsk.h`** | `$FF80-$FF86` | `turbo9os`, `flat65280`, `deep65280`, `gep9` | VFS-backed 256-byte sector block disk (`drive0..drive3` on `/pc/` or `/lfs/`). |
| **`floppy_wd1793.h`** | `$FF40`, `$FF48-$FF4B` | `centipede-32z` (CoCo 2/3 DECB & NitrOS-9) | `IN_RAM` WD1773/1793 state machine backed by Core 0 `floppy_task` coroutine. |
| **`fuzix_coco_disk.h`** | `$FF50-$FF58` | `centipede-32z` (`FUZIX_COCO2CENTIPEDE`) | DMA block disk transferring 256B sectors between `/f0` and active CoCo 3 MMU bank. |
| **`sd_multicomp09.h`** | `$FFD2-$FFD7` | `tfr911h` (`multicomp09`) | 512B sector SD card state machine for Fuzix. |
| **`fujinet_becker.h`** | `$FF45/$FF46` (or `$FF41/$FF42`) | `tfr911h` & `centipede-32z` | Core 0 `uart0` ISR (`GPIO 12/13`, 921600 baud) + 4 KB RX / 1 KB TX ring buffers + Core 1 `IN_RAM` Becker status/data handlers + Tcl `fujinet` command. |
| **`audio_sn76489_pwm.h`** | `$FF41` (SN76489), `$FF42` (click/bell/raw), `$FF7A/$FF7B` (Orchestra-90) | `tfr911h` (`GPIO 8`) & `centipede-32z` (`GPIO 26`) | Unifies Andrew Diller's 31.25 kHz PWM SN76489/keyclick/bell synth with `orchestra90.h` using `BOARD_AUDIO_PIN`. |
| **`hw_info_port.h`** | `$FF43` (`$A5` trigger), `$FF44` (stream read) | `tfr911h` & `centipede-32z` | Streams RP2350B clock, uptime, true ADC ch 8 die temp, RAM size, and FujiNet/disk counters to OS-9 `info` tool. |
| **`rtc_jvemu.h`** | `$FFC0-$FFC7` (read), `$FFDC`/`$FFDD` (timer) | `tfr911h` (& optional on `centipede-32z` when not colliding with SAM) | Provides `clock2_jvemu` binary time registers populated from Tether `T_WALLTIME`. |
| **`keyboard_injector.h`** | `$FF00` (read), `$FF02` (write) | `centipede-32z` | Double-buffered `probe_table[2][256]` for zero-wait CoCo matrix keystroke injection. |

### 6.2 How FujiNet Works Seamlessly on Both `centipede-32z` and `tfr911h`

Because `hat-fuji-41d` (and `40c-bodged`) uses **`GPIO 12..15`** on the 2x10 `J3` header — which are completely unused by the 6809 bus on **both** `tfr911h` and `centipede-32z` — the exact same `rp2350/io_emu/fujinet_becker.h` module works on both boards:
1. **On `tfr911h`**:
   - Core 0 initializes `uart0` on `GPIO 12` (TX) and `GPIO 13` (RX) at 921,600 baud and enables `UART0_IRQ`.
   - Core 1 registers `fujinet_becker` at `$FF45/$FF46` (and optionally `$FF41/$FF42` if SN76489 is disabled) in `IOReaders`/`IOWriters`.
2. **On `centipede-32z` (CoCo 1/2/3)**:
   - Core 0 initializes the exact same `uart0` ISR on `GPIO 12`/`GPIO 13`.
   - During CoCo bus cycles where `SCS*` (`GPIO 9`) is low (`$FF40-$FF5F`), Core 1 already decodes `$FF40` and `$FF48-$FF4B` for the virtual floppy (`floppy_wd1793.h`) and `$FF50-$FF58` for Fuzix disk (`fuzix_coco_disk.h`).
   - By adding `$FF45/$FF46` (or `$FF41/$FF42` when HDB-DOS / NitrOS-9 Becker mode is active) to the `SCS*` decode in `centipede-32z/main.cpp`, **any CoCo 1, 2, or 3 with a Centipede 32Z + `hat-fuji-41d` immediately gets a hardware Becker port backed by the ESP32 FujiNet at 921,600 baud**, while simultaneously keeping virtual floppy emulation at `$FF40/$FF48-$FF4B` and Tcl `fujinet mount` support!

### 6.3 Unified `tether` Go Tool (`v5/tether/`)

Instead of maintaining three separate `tether` directories, `v5/tether/` will be a single Go module producing the `tether` binary:
- **Shared Core**: `cobs`, `serial` (with Linux `/dev/ttyACM*` fallback), `pcb`, `rpc`, `os9` (`os9api`, `os9err`, `defs_level1`, `defs_level2`), `decode_m6809e`, `listings` (including OS-9 in-RAM module header scanner from `turbolab`).
- **All Flag & Command Capabilities Included**:
  - Standard console & VFS/Disk server (`disk.go` with `EmulateDiskWrite` bug fixed, UTF-8 terminal passthrough, `-pass_ctrl_c`, `T_WALLTIME`).
  - `tfr911h` extensions: `--tcl_input`, `--reconnect`, `C_CORE_DUMP` (189), `C_SHUTDOWN` (255) + `Dump6309PanicState`.
  - `centipede-32z` extensions: `--coco3`, `~load` / `~save` (`bas_token.go`), `--quick-get-ram`, `--quick-get-text`, `--quick-get-pmode`, `--quick-get-hscreen`, `--quick-get-cycles`, `--quick-type`, live web server (`webserver.go` + `samvdg.go` with GIME text/graphics and 256 KB Fuzix MMU support).
  - `turbolab` mode (activated via `tether turbolab ...` or `--turbolab` flag): runs the `STATE_WAIT_CONFIG` handshake (`tuning`, `poke`, `watch`, `start`), streams `C_TRACE_CYCLES` (200) through the relocated `.list` / `Os9Tracer` pipeline, and decodes `C_FAULT` (188) SWI-harvested CPU registers.

---

## 7. Step-by-Step Migration Roadmap for `v5/`

We recommend building `v5/` in five incremental phases (copying and adapting from `../v4/` and `~/github.com/dillera/tfr9`, never modifying `../v4/`):

1. **Phase 1 — Shared RP2350B Foundation (`rp2350/`)**:
   - Copy `littlefs/`, `tcl6.7c/`, and `miniz/` into `v5/rp2350/third_party/`.
   - Create `v5/rp2350/boards/tfr911h_rp2350b.h` (with `#define PICO_RP2350A 0`!) and `centipede_32z_rp2350b.h`.
   - Populate `v5/rp2350/util/` (using `three/util/cobs.h`) and `v5/rp2350/core0/` (merging the best of `one/firmware/` and `three/firmware/` as detailed in Table 2.1).
   - Verify host unit tests in `rp2350/util/tests/` pass.

2. **Phase 2 — Unified `tether` (`v5/tether/`)**:
   - Merge `v4/tether/`, `v4/centipede-32z/tether/`, and `v4/turbolab/tether/` into `v5/tether/`.
   - Apply Andrew Diller's fixes (`disk.go` variable-shadowing fix, `tconsole.go` UTF-8 fix, `-pass_ctrl_c`, `T_WALLTIME`).
   - Run `go test ./...` across all packages (COBS, BASIC tokenizer, 6809 disassembler, panic dumper, web server, quick-get-text).

3. **Phase 3 — Composable 6809/6309 I/O & MMU Library (`rp2350/io_emu/` & `rp2350/pio/`)**:
   - Extract the PIO programs (`hamster.pio`, `pio_assembler.h`, `gerbil.pio`) into `v5/rp2350/pio/`.
   - Extract the MMU policies (`mmu_flat64k.h`, `mmu_coco_sam.h`, `mmu_coco3_gime.h`, `mmu_multicomp09.h`, `mmu_gep9.h`) and I/O devices (`acia_6850.h`, `turbo9_uart.h`, `emudsk.h`, `floppy_wd1793.h`, `sd_multicomp09.h`, `fuzix_coco_disk.h`, `fujinet_becker.h`, `audio_sn76489_pwm.h`, `rtc_jvemu.h`, `hw_info_port.h`, `keyboard_injector.h`, `gspoon_console.h`, `trace_and_fault.h`) into `v5/rp2350/io_emu/`.

4. **Phase 4 — Target Firmwares (`v5/firmware/`)**:
   - Build `firmware/tfr911h/`, `firmware/centipede-32z/`, `firmware/centiscope/`, and `firmware/turbolab/` against the shared `rp2350` CMake targets.
   - Add the `check-core1-sram` build check to ensure no Core 1 bus-loop function or table is placed in XIP Flash.

5. **Phase 5 — OS Recipes, FujiNet Suite & System Tests (`v5/os/`, `v5/systests/`)**:
   - Import the Tcl boot scripts (`mode*.tcl`, `unix.tcl`), OS recipes (TurboOS, NitrOS-9 L1/L2, Fuzix, Hatvan, BASIC), and Andrew Diller's `nitros9-l2/fujinet/` + `cmds/` suite (fixing `term.c` so it uses OS-9 `I$WritLn` rather than hardcoded `$FF15`).
   - Bring over `systests/` for automated verification across `tfr911h`, `centipede-32z`, and `turbolab`.
