# TFR911H v4 Firmware Specification & Architecture Guide

> **Audience**: Systems engineers, kernel developers, and retro-computing enthusiasts working on the TFR911H hardware emulator, NitrOS-9 OS integration, and RP2350 microcontroller firmware.

---

## 1. System Overview

The **TFR911H v4 firmware** (`tfr911h_v4.uf2`) runs on a **Raspberry Pi RP2350B** (Dual ARM Cortex-M33 @ 250 MHz) to control and service a physical **Hitachi 6309E** microprocessor. 

Rather than purely software-emulating the CPU, the RP2350B acts as the motherboard chipset:
- It synthesizes the quadrature **E** and **Q** system clocks via a dedicated PIO state machine.
- It supplies 64 KB of high-speed SRAM, mapped to the 6309E bus cycles.
- It handles memory-mapped hardware devices (serial ACIA, timer clock, virtual disk controllers) across Core 0 and Core 1.
- It exposes an interactive **Tcl 6.7 Shell** and **VFS (Virtual File System)** layer providing transparent access to on-chip flash (**LittleFS**) and host PC files (**TetherFs** over USB).

```text
+-------------------------------------------------------------------------------+
|                                    THE PC                                     |
|  Host workstation running `tether` (Go)                                       |
|  - USB CDC Serial pipeline (COBS-encoded packets)                             |
|  - Virtual File System server (`/pc/...` mapped to host directory tree)       |
+-------------------------------------------------------------------------------+
                                      ▲
                                      │ USB CDC (COBS Packets)
                                      ▼
+-------------------------------------------------------------------------------+
|                             THE RP2350B (PICO 2)                              |
|                                                                               |
|  +-------------------------------------+  +--------------------------------+  |
|  |       Background Core (Core 0)      |  |    Foreground Core (Core 1)    |  |
|  | - Coroutine Scheduler (`Coro`)      |  | - Zero-wait-state bus engine   |  |
|  | - Tcl 6.7 REPL & Config Menu        |  |   running completely in SRAM   |  |
|  | - LittleFS & Tether VFS Client      |  | - 64 KB flat RAM (`ram[]`)     |  |
|  | - EMUDSK sector read/write worker   |  | - Fast `IOReaders`/`IOWriters` |  |
|  | - USB CDC packet pump               |  | - Physical IRQ pin control     |  |
|  +-------------------------------------+  +--------------------------------+  |
|                      ▲                                    │                   |
|                      │   Cross-Core SPSC Ring Buffers     │                   |
|                      └── (fg2bg, fg2bg_chars, bg2fg) ◄────┘                   |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |                    "The Hamster" (PIO0 State Machine 0)                 |  |
|  | - Quadrature E and Q clock generator (Pins 29 & 30)                     |  |
|  | - Address and data bus timing synchronization                           |  |
|  +-------------------------------------------------------------------------+  |
+-------------------------------------------------------------------------------+
                                      ▲
                                      │ 48 GPIO Pins (Address, Data, Clocks, Control)
                                      ▼
+-------------------------------------------------------------------------------+
|                               THE CPU (6309E)                                 |
|  Physical Hitachi 6309E microprocessor (6809E mode)                           |
|  - Executes machine instructions directly from RP2350 SRAM                    |
|  - Bus cycles synchronized to PIO quadrature clocks                            |
+-------------------------------------------------------------------------------+
```

---

## 2. Firmware Lifecycle

The firmware operates in two distinct phases:

### Phase 1: Pre-Boot & Interactive Tcl REPL (Core 0)
1. On power-up, the RP2350 initializes GPIOs, system clocks (250 MHz), USB stdio, and the PIO Hamster program.
2. The 6309E is held in **RESET** and **HALT** (active-low open-drain lines pulled low).
3. Core 0 launches the **Tcl 6.7 REPL** over USB serial (`TCL> ` prompt).
4. In this shell, users and automated tether scripts can:
   - Browse files (`ls`, `cat`, `cd`, `pwd`, `cp`, `mkdir`).
   - Access internal flash (`/lfs/...`) or host workstation files (`/pc/...`).
   - Query or set configuration parameters via `menu fetch Config` and `menu store Config`.
5. Typing `bye` (or executing `bye` in a script) finishes Phase 1.

### Transition Phase
1. Core 0 inspects the `Config` settings stored in `centipede_config`.
2. If `Config(engine)` is set to `"flat65280"`:
   - Drive nodes (`drive0`, `drive1`) are resolved via VFS and opened (`LFS_O_RDWR` or `LFS_O_RDONLY`).
   - The OS boot image (`flat_img`, defaulting to `/pc/flat65280.img`) is loaded into `ram[]`.
   - Hardware I/O handlers (`Install_Flat65280_IO()`) are registered into the `IOReaders` and `IOWriters` jump tables.
3. If `Config(engine)` is `"turbo9os"` (default):
   - Legacy Turbo9OS ROMs and vectors are installed.
4. Core 0 resets the PIO Hamster state machine and launches Core 1 (`multicore_launch_core1(Engine__Foreground)`).
5. Core 0 waits for Core 1 to signal `foreground_running = true`.
6. Core 0 releases **RESET** and **HALT**; the 6309E fetches its reset vector and starts execution.

### Phase 2: Active 6309 Execution (Core 0 + Core 1)
- **Core 1 (Foreground)**: Runs the inner bus cycle loop. For every E/Q clock cycle:
  - If `addr < 0xFF00`: accesses `ram[addr]` directly in SRAM.
  - If `addr >= 0xFF00`: dispatches to `IOReaders[addr & 0xFF]` or `IOWriters[addr & 0xFF]`.
  - Asserts or releases the physical **IRQ** pin (GPIO 22) as needed.
- **Core 0 (Background)**:
  - Continuously executes the coroutine scheduler (`drain_coro`).
  - Pumps USB packets (`PumpUsbCobs()`).
  - Transfers terminal keyboard input to the 6850 ACIA emulator.
  - Services synchronous block disk transfers (`FG2BG_EMUDSK`) requested by Core 1.

---

## 3. Supported Engines

### 3.1 Turbo9OS (`turbo9os`, Default)
- Memory map: Split RAM/ROM model. OS ROMs (`turbo9os.rom.h`, `basic09.mod.h`, `ax.mod.h`) are copied into high memory below `$FF00`.
- Terminal: Proprietary Turbo9Sim ACIA registers at `$FF00..$FF03`.
- Clock: 60 Hz timer interrupt driving simulated ACIA control bits.

### 3.2 Flat65280 (`flat65280`)
A clean, flat 64 KB memory model designed for NitrOS-9 Level 1 direct boot:
- **Zero MMU Requirement**: Flat memory space with no banking hardware needed.
- **Memory Split**:
  - `$0000..$FEFF` (65,280 bytes): **Flat RAM**. Houses NitrOS-9 system variables, bitmap, module directory, stack, free memory, and preloaded OS modules abutting `$FEFF`.
  - `$FF00..$FFFF` (256 bytes): **I/O Red Page & Vectors**.
- **Image Loading**: Boots by loading `/pc/flat65280.img` (or configured `flat_img` path) from host/flash via VFS into RAM.

---

## 4. Hardware I/O Register Specification (`flat65280`)

All peripheral registers are mapped to `$FF00..$FFFF`. To guarantee that no flash XIP cache misses ever stall a physical 6309E bus cycle, every register callback is compiled with `IN_RAM` (`__not_in_flash`).

| Address Range | Device / Function |
|---|---|
| `$FF80..$FF86` | **EMUDSK** Virtual Block Storage Controller |
| `$FF87` | **CLOCK_AND_STOP** Periodic Timer & Emulator Control |
| `$FF88..$FF89` | **ACIA 6850** Terminal Console UART |
| `$FFF0..$FFFF` | **System Vectors** (Trap, SWI3, SWI2, FIRQ, IRQ, SWI, NMI, RESET) |

---

### 4.1 EMUDSK (`$FF80..$FF86`)

EMUDSK is the standard NitrOS-9 virtual hard disk interface (`level1/modules/emudsk.asm`). It executes synchronous DMA sector transfers between the host storage subsystem and system RAM.

#### Register Map
| Address | Access | Description |
|---|---|---|
| `$FF80` | R/W | `LSN[23:16]` — High byte of 24-bit Logical Sector Number |
| `$FF81` | R/W | `LSN[15:8]` — Mid byte of 24-bit Logical Sector Number |
| `$FF82` | R/W | `LSN[7:0]` — Low byte of 24-bit Logical Sector Number |
| `$FF83` | R / W | **Write**: `COMMAND` (`0` = Read Sector, `1` = Write Sector).<br>**Read**: `STATUS` (`0` = Success, `2` = Not Ready, `6` = CRC/IO Error). |
| `$FF84` | R/W | `BUFFER[15:8]` — High byte of 16-bit destination RAM address |
| `$FF85` | R/W | `BUFFER[7:0]` — Low byte of 16-bit destination RAM address |
| `$FF86` | R/W | `DRIVE` — Drive select (`0` = Drive 0, `1` = Drive 1) |

#### Cross-Core Synchronization & Bus Pausing
1. The 6309E writes `COMMAND` (`sta >$FF83`).
2. Core 1 populates the global `EmuDskRequest` struct:
   ```cpp
   g_emudsk_req.drive = emudsk_drive;
   g_emudsk_req.lsn = (lsn0 << 16) | (lsn1 << 8) | lsn2;
   g_emudsk_req.buffer_addr = (buf0 << 8) | buf1;
   g_emudsk_req.command = data;
   g_emudsk_req.status.store(-1, std::memory_order_release);
   ```
3. Core 1 posts `FG2BG_EMUDSK` into the `fg2bg` lock-free ring buffer and enters a tight spin-wait loop:
   ```cpp
   while (g_emudsk_req.status.load(std::memory_order_acquire) < 0) {
     tight_loop_contents();
   }
   ```
4. **Bus Stalling**: While Core 1 spin-waits, it holds the bus cycle at Phase 3 ($E=0, Q=0$). Because the Hitachi 6309E utilizes fully static internal registers, pausing the clock for multiple milliseconds during disk I/O causes zero data corruption or state loss.
5. **Core 0 Execution**:
   - `drain_task` pops `FG2BG_EMUDSK`.
   - It checks `g_drive_nodes[drv]`. If null, status is set to `2` (`E$NotRdy`).
   - It seeks `node->seek(lsn * 256, LFS_SEEK_SET, &self)`. If seeking a TetherFs file over USB, the coroutine yields cooperatively to let the USB pipeline receive host packets.
   - It reads or writes 256 bytes directly into/from `ram[buffer_addr]`.
   - It updates `g_emudsk_req.status.store(0)`.
6. Core 1 detects `status >= 0`, latches the return status into `emudsk_status`, finishes the bus cycle, and releases the 6309E.
7. The 6309E immediately executes `lda >$FF83`, receiving the completion status without needing interrupts or polling loops.

---

### 4.2 CLOCK_AND_STOP (`$FF87`)

A multi-frequency periodic tick generator and immediate emulator lifecycle port:

#### Register Bits
- **Bit 0 (`TICK`, R/W1C)**:
  - **Read**: `1` if a clock tick is pending; `0` if idle.
  - **Write**: Writing `1` clears the tick flag. Writing `0` leaves it unaltered.
- **Bit 1 (`IRQ_EN`, R/W)**: `1` enables physical IRQ assertion; `0` masks IRQ.
- **Bits 3..2 (`USER[1:0]`, R/W)**: Preserved general-purpose bits.
- **Bits 5..4 (`RATE[1:0]`, R/W)**: Clock frequency selector:
  - `00` (`$00`): **60 Hz** (default interval: ~16.67 ms)
  - `01` (`$10`): **50 Hz** (interval: 20 ms)
  - `10` (`$20`): **6 Hz** (interval: ~166.7 ms, default for NitrOS-9 `flat65280` `TkPerSec=6`)
  - `11` (`$30`): **0.1 Hz** (interval: 10 seconds)
- **Bits 7..6 (`USER[3:2]`, R/W)**: Preserved general-purpose bits.

#### Abrupt Control Commands
Writing specific escape bytes to `$FF87` invokes instant firmware control:
- **`$FC`**: Prints exit notification and resets to USB bootloader (`reset_usb_boot(0, 0)`).
- **`$FD`**: Prints exit notification with status code 1.
- **`$FE`**: Induces a deliberate hardware panic / core dump (`panic()`).

#### Physical Interrupt Generation
A repeating 60 Hz timer callback on Core 0 evaluates:
```cpp
bool flat65280_irq_needed() {
  bool clock_irq = (clock_and_stop_reg & 0x01) && (clock_and_stop_reg & 0x02);
  bool acia_irq = ((flat65280_acia_ctrl & 0x80) != 0) && flat65280_rx_has_char;
  return clock_irq || acia_irq;
}
```
If `irq_needed` is true, Core 1 sets the direction of **GPIO 22** (`IRQ`) to `GPIO_OUT` (asserting active-low line). When acknowledged by the CPU ISR clearing the tick or consuming the ACIA byte, GPIO 22 returns to `GPIO_IN` (open-drain pull-up).

---

### 4.3 ACIA 6850 (`$FF88..$FF89`)

Standard Motorola 6850 UART interface used by NitrOS-9 `sc6850` driver for `/Term`:
- **`$FF88` (Status / Control)**:
  - **Read (Status)**:
    - Bit 0 (`RDRF`): `1` if a byte is waiting in the input buffer.
    - Bit 1 (`TDRE`): Always `1` (host USB output buffer is always ready).
    - Bit 7 (`IRQ`): `1` if `RDRF=1` and Rx interrupt enable is set in Control.
  - **Write (Control)**: Bit 7 enables Rx IRQ. Bits 1..0 set master reset or clock divisor.
- **`$FF89` (Data)**:
  - **Read**: Consumes the received character from Core 0's `term_input` buffer, resetting `RDRF` to 0.
  - **Write**: Immediately emits the character to the USB CDC stream via `Say(data)` (pushed into `fg2bg_chars`).

---

### 4.4 System Vectors (`$FFF0..$FFFF`)

The top 16 bytes contain 6809/6309 CPU exception vectors. In the `flat65280` engine:
- `IOReaders[0xF0..0xFF]` execute `ram_vector_reader(addr)`, returning `ram[0xFF00 | (addr & 0xFF)]`.
- The reset vector at `$FFFE..$FFFF` points directly to the `OS9Cold` entry point of `krn` (`$0811` in `flat65280.img`).

---

## 5. Configuration via Tcl & the `menu` Command

The firmware adopts the `menu store <arrayName>` paradigm established in `centipede-32z`. No custom C++ Tcl commands are required to configure engines; settings are passed using standard Tcl array variables.

### Supported Configuration Keys
| Key | Type | Description | Default |
|---|---|---|---|
| `engine` | string | Target engine: `"flat65280"` (or `"flat"`) vs `"turbo9os"`. | `"turbo9os"` |
| `flat_img` | string | VFS path to 64 KB OS boot image. If empty or absent, loads embedded flash fallback. | `""` |
| `drive0` | string | VFS path to disk image for EMUDSK Drive 0 (e.g. `/pc/...` or `/lfs/...`). | `""` |
| `drive1` | string | VFS path to disk image for EMUDSK Drive 1. | `""` |

### Interactive Setup Example
From the TFR911 USB serial console:
```tcl
TCL> set Config(engine) "flat65280"
TCL> set Config(flat_img) ""
TCL> set Config(drive0) "/pc/recipes/flat65280/flat0.dsk"
TCL> set Config(drive1) ""
TCL> menu store Config
TCL> bye
```

### Automated Script Execution
A boot script is provided in the repository at [`recipes/flat65280/boot_tfr9.tcl`](file:///home/strick/modoc/coco-shelf/nitros9/recipes/flat65280/boot_tfr9.tcl):
```tcl
# boot_tfr9.tcl - Launch flat65280 engine on TFR911 firmware
set Config(engine) "flat65280"
set Config(flat_img) "" ;# empty string uses embedded fallback image
set Config(drive0) "/pc/recipes/flat65280/flat0.dsk"
set Config(drive1) ""
menu store Config
bye
```
To boot in one command from the Tcl prompt:
```tcl
TCL> source /pc/recipes/flat65280/boot_tfr9.tcl
```

---

## 6. Build Instructions

### Prerequisites
- Raspberry Pi Pico SDK (v2.x recommended for RP2350 support).
- CMake 3.20+ and ARM GCC (`arm-none-eabi-gcc`).

### Compiling Firmware
From the root of the repository tree:
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/tfr911h
mkdir -p build && cd build
cmake ..
make -j
```

### Build Artifacts
- **`tfr911h_v4.uf2`**: Ready to drag-and-drop onto the RP2350 USB mass-storage bootloader (`RPI-RP2`).
- **`tfr911h_v4.bin`**: Raw binary for flashing via `picotool` or SWD.
- **`tfr911h_v4.elf`**: Complete ELF executable with symbols for debugging.
