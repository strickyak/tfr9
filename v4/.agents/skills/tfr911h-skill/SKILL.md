---
name: tfr911h-skill
description: >-
  Guide and reference for compiling, reflashing, running, and debugging the
  TFR/911h single board computer (TurboLab). Includes procedures for firmware
  compilation, Tether host tool builds, software reflash into BOOTSEL mode,
  interactive OS-9 sessions, and detailed bus tracing and packet debugging
  using --trace and --debug flags.
---

# TFR/911h (TurboLab) Skill

This skill provides a comprehensive operational guide for the **TFR/911h Single Board Computer** (Hitachi 6309E CPU + Raspberry Pi RP2350B co-processor) running under the **TurboLab** configuration in [`v4/turbolab/`](file:///home/strick/modoc/coco-shelf/tfr9/v4/turbolab/).

---

## 1. System Architecture Overview

The TFR/911h system operates in a tethered architecture:

- **RP2350B Microcontroller (Firmware)**:
  - **Core 0 (Background)**: Manages USB CDC-ACM communication with the PC host at 115200 baud, handles Proto-Crawl Buffer (PCB) RPCs (`config`, `upload`, `start`), manages firmware state transitions, and streams console and trace packets over USB.
  - **Core 1 (Foreground)**: Runs the real-time 6309 bus engine in RAM ([`foreground_loop`](file:///home/strick/modoc/coco-shelf/tfr9/v4/turbolab/firmware/main.cpp#L208)). Synchronizes with the **Hamster PIO** state machine to generate dynamic E and Q bus clocks (~1.63 MHz), handles address decoding, responds to 6309 read/write cycles, emulates the Turbo9Sim ACIA peripheral registers ($FF00..$FF03), and pushes trace records and console characters to Core 0 via lock-free cross-core FIFOs.
- **6309E CPU**: Executes 6809/6309 machine code (OS-9 Level 1 operating system).
- **Host PC (Tether Tool)**: A Go program ([`turbolab/tether`](file:///home/strick/modoc/coco-shelf/tfr9/v4/turbolab/tether/)) that connects over USB serial, manages handshake beacons, configures trace and limits, loads the 64KB RAM image, starts CPU execution, maps disassembled assembly listings against live bus cycles, and provides an interactive terminal session.

---

## 2. How to Compile

All build operations are managed via [`turbolab/Makefile`](file:///home/strick/modoc/coco-shelf/tfr9/v4/turbolab/Makefile).

### Prerequisites & Paths
The build relies on the development shelf tools located in `coco-shelf`:
- Pico SDK: `coco-shelf/pico-sdk`
- Picotool: `coco-shelf/picotool-install` and `coco-shelf/bin/picotool`
- ARM Cross Toolchain: `arm-none-eabi-gcc` / `arm-none-eabi-g++`
- CMake: `>= 3.13`
- Go: `>= 1.20`

### Compile Everything
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
make
```
This builds both the RP2350 firmware UF2 image and the host Tether binary.

### Compile Firmware Only
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
make build/turbolab_v4.uf2
```
- Builds in `build/firmware/` using CMake and Pico SDK with board target `tfr911h_rp2350b`.
- Generates `build/turbolab_v4.uf2` (~98 KB).

### Compile Tether Only
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
make build/tether
```
- Compiles the Go tether program located in `tether/`.
- Generates `build/tether` binary.

---

## 3. How to Reflash the Firmware

### Method 1: Software Reflash via Tether (Recommended)
When the Pico is already connected and running any TurboLab firmware version with reflash support:

```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
./build/tether --reflash
```

1. Tether connects to `/dev/ttyACM0` at 115200 baud.
2. Awaits or requests the `C_RESTARTED` beacon.
3. Transmits the protocol command `T_REFLASH_NOW_PLEASE` (cmd `175`).
4. The Pico firmware asserts 6309 `RESET` and `HALT`, shuts down Core 1 and the 60Hz timer, stops Hamster PIO, and invokes `reset_usb_boot(0, 0)`.
5. Tether announces the transmission, safely restores terminal settings, and exits 0.
6. The RP2350 drops CDC serial and mounts as a USB mass storage drive at `/media/$USER/RP2350` (or `/media/$USER/RP*`).

### Method 2: Standard 1200-Baud CDC Touch
If the tether tool is not running or as an alternative:
```bash
stty -F /dev/ttyACM0 1200
```
This triggers the Pico SDK USB CDC bootloader reset.

### Method 3: Hardware BOOTSEL
Hold the hardware `BOOTSEL` button on the RP2350 board while plugging in the USB cable or tapping the hardware reset button.

### Copying the UF2 Image to the Board
Once the board appears as a mass storage volume:
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
make flash
```
*(Or manually: `cp -fv build/turbolab_v4.uf2 /media/$USER/RP*`)*

The RP2350 flashes the new firmware to SPI flash and automatically reboots within ~1–2 seconds, re-enumerating as `/dev/ttyACM0`.

### Board LED Indicator Reference
- **Flashing at 2 Hz (50% duty cycle, 250ms ON / 250ms OFF)**: `STATE_WAIT_CONFIG`. The board is in restarted mode, sending periodic beacons and waiting for Tether to connect.
- **Turns ON**: During 6309 interrupt handling (CPU reads vector in `$FFF0..$FFFF`).
- **Turns OFF**: When the CPU fetches the `RTI` ($3B) instruction.

---

## 4. How to Run

### Basic Invocation
To boot OS-9 on the 6309 CPU with primordial modules and source disassembly listings:

```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
./build/tether data/turbos/turbos_dev.img data/turbos/*.list
```

### Execution Lifecycle
1. **Listing Resolution**: Tether scans `turbos_dev.img` for OS-9 module headers (`kernel`, `init`, `tk`, `ioman`, `scf`, `scvt`, `term`, `shell`, `mdir`, `mfree`, `procs`, `sleep`). It parses the provided `.list` files and relocates them to each module's base address in the 64KB image.
2. **Device Connection**: Connects to `/dev/ttyACM0` at 115200 baud.
3. **Pico Handshake**: Listens for the `C_RESTARTED` beacon. If no beacon is heard after 2 seconds (e.g., if the Pico was already running), Tether sends `T_RESTART_NOW_PLEASE` (`#`). The Pico resets internal state and replies with `C_RESTARTED` (`%`).
4. **Configuration**: Tether sends the `config` RPC with active trace flags, trigger settings, and stop cycle/time limits.
5. **Image Upload**: Uploads the 64KB memory image in 1KB chunks via `upload` RPCs.
6. **CPU Launch**: Tether issues the `start` RPC. The firmware resets the cycle counter to zero, releases 6309 `HALT`, and starts the 60Hz periodic clock.
7. **Interactive Terminal**: Tether sets the local terminal to `cbreak` mode (`-echo`, `-ixon`). You are placed in the OS-9 shell:
   ```text
   Shell

   TOS:
   ```
   Type standard OS-9 commands:
   - `mdir -e`: List memory module directory with addresses, sizes, and attributes.
   - `mfree`: Display free memory map.
   - `procs`: Display process table.

### Stopping or Interrupting Tether
- Press **`Ctrl-C`** (`SIGINT`): Tether intercepts `SIGINT`, immediately restores the terminal `stty` state to normal cooked mode, prints `\n[SIGINT]\n` on `stdout`, logs `Interrupted by SIGINT (^C); tether exiting.` on `stderr`, and exits with code `130`.

---

## 5. Debugging with `--debug` and `--trace`

Tether cleanly separates output streams:
- **`stdout`**: Reserved strictly for 6309 console output (characters printed by OS-9) and critical lifecycle marks (`#`, `%`, `[USB Connected]`, `[SIGINT]`).
- **`stderr`**: Receives all diagnostic logs, USB packet traces, bus cycle disassembly lines, upload progress, and fault information.

### Recommended Log Capture Pattern
Always separate `stdout` and `stderr` when debugging:
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab
./build/tether -trace=x --debug=u data/turbos/turbos_dev.img data/turbos/*.list 2>_log | tee _out
```
- `_out`: Interactive console dialogue.
- `_log`: Diagnostic trace and packet logs.

---

### Using `--debug` (Packet Inspection)

- `--debug=u`: Enables detailed USB COBS packet logging on `stderr`.
  - Stale packets from prior sessions arriving before CPU start are filtered automatically.
  - Formats every packet sent to or received from the Pico:
    ```text
    PACKET OUT: cmd=181(T_PICO_RPC) len=11 hex=b50a057374617274114200
    PACKET IN:  cmd=181(T_PICO_RPC) len=4 hex=b5114200
    PACKET IN:  cmd=200(C_TRACE_CYCLES) len=62 hex=c805...
    ```

---

### Using `--trace` (Bus Cycle Disassembly)

The `--trace` flag controls which 6309 bus cycles are captured by Core 1 and transmitted to Tether:

| Flag | Name | Description | Example Output Format |
|---|---|---|---|
| `x` | **FIC** (First Instruction Cycle) | Opcode byte fetch of each executed instruction. Cross-referenced with relocated listing files. | `x D4F2 8E #9; "kernel.0d4eec829c"+0014   ldx #D.FMBM start clearing memory at D.FMBM` |
| `+` | **OPCODE_CONT** | Subsequent operand / address bytes of multi-byte instructions. | `+ D4F3 00 #10; ` |
| `r` | **READ** | Data read bus cycles (also implies `r,x,i,+`). | `r 0020 00 #11; ` |
| `w` | **WRITE** | Data write bus cycles. | `w 0020 00 #15; ` |
| `i` | **INTERRUPT** | Hardware interrupt vectors and `RTI` returns. | `i IRQ     #12045; ` or `i RTI     #12100; ` |
| `t` | **TRAP** | OS-9 system call traps (`SWI2`). | `t SWI2    #3500; ` |
| `1` | **ALL** | Shortcut enabling all trace flags (`x,+,r,w,i,t`). | Full cycle-by-cycle execution log. |

#### Formatting Breakdown
```text
x D4F2 8E #9; "kernel.0d4eec829c"+0014   ldx #D.FMBM start clearing memory at D.FMBM
│  │   │  │   │                          │
│  │   │  │   │                          └─ Disassembled assembly source line from .list
│  │   │  │   └─ Module name, size, CRC, and hex offset within the module
│  │   │  └─ Bus cycle count since 6309 CPU release (HALT deasserted)
│  │   └─ Data byte on the data bus
│  └─ 16-bit address on address bus
└─ Cycle kind ('x'=opcode, '+'=operand, 'r'=read, 'w'=write, 'i'=interrupt)
```
*Note: If an instruction executes outside known listings, Tether still prints the cycle with module offset or empty comment (e.g. `x 010C 20 #120; `).*

---

### Triggers and Execution Limits

Use triggers and limits to isolate specific bug windows and prevent log files from growing uncontrollably:

#### 1. Delayed Tracing (`--trigger`)
Suppress trace printing until a specific point in execution:
- `--trigger=c:<cycles>`: Begin emitting trace only after reaching cycle count:
  ```bash
  ./build/tether -trace=x --trigger=c:500k data/turbos/turbos_dev.img data/turbos/*.list
  ```
- `--trigger=s:<seconds>`: Begin emitting trace after elapsed wall-clock duration:
  ```bash
  ./build/tether -trace=x --trigger=s:2.5 data/turbos/turbos_dev.img data/turbos/*.list
  ```
- `--trigger=[r|w|x:]<addr>[:N]` or `--trigger=@modulename[+offset][:N]`: Begin emitting trace on the Nth read (`r:`), write (`w:`), or execution (`x:` / default) of an address or module symbol:
  ```bash
  ./build/tether --trigger=x:@kernel+0x1B data/turbos/turbos_dev.img data/turbos/*.list
  ```

#### 2. Automatic Termination (`--stop`)
Halt the 6309 CPU and trigger a core dump once a limit is reached (flag `--stop`, alias `--max`):
- `--stop=c:<cycles>`: Terminate after cycle count:
  - Suffixes: `k=1000`, `m=1000000`, `g=1000000000` (decimal SI) and `K=1024`, `M=1048576`, `G=1073741824` (binary).
  ```bash
  ./build/tether -trace=x --stop=c:64K data/turbos/turbos_dev.img data/turbos/*.list
  ```
- `--stop=t:<duration>`: Terminate after time duration (e.g. `500ms`, `5s`, `1m`):
  ```bash
  ./build/tether -trace=x --stop=t:10s data/turbos/turbos_dev.img data/turbos/*.list
  ```
- `--stop=[r|w|x:]<addr>[:N]` or `--stop=@modulename[+offset][:N]`: Terminate on Nth watchpoint event:
  - Supports decimal or hexadecimal offsets: `@kernel+27`, `@kernel+0x1B`, `@kernel+$1B`.
  - Shorthand `@modulename+offset` defaults to execution (`x:`).
  ```bash
  ./build/tether -n --stop=@kernel+0x1B data/turbos/turbos_dev.img
  ```

#### 3. Logging Watchpoints (`--watch`)
Log individual matching bus cycles to `stderr`, even when `--trace` is disabled or before `--trigger` has fired:
- Target addresses: numeric literals (`0x0020`, `$0020`, `32`) or OS-9 module offsets (`@kernel+0x1B`).
- Match cycle kinds: default is all cycles (reads, writes, fetches), or `r:addr`, `w:addr`, `x:addr`.
- Optional Nth count filter: `addr:N` (e.g. `0x1019:3` logs only the 3rd hit).
- Firmware hook: high-performance function pointer (`wp_hook`) in tight loops; zero overhead when unused.
```bash
./build/tether -n --watch=0x0020 --stop=c:50k data/turbos/turbos_dev.img
./build/tether --watch=w:0x0020 --trigger=@kernel+0x1B --trace=x data/turbos/turbos_dev.img
```

---

### Hardware Faults and Core Dumps

The firmware monitors the bus for invalid hardware and vector states:
- **Red Page Violation (`FAULT_RED_PAGE`)**: Access to reserved hardware addresses in range `$FF04..$FFEF`.
- **Zero Interrupt Vector (`FAULT_ZERO_VECTOR`)**: An interrupt acknowledge occurs (`BS=1`) but the vector address byte reads `$00`.
- **Infinite Self-Branch (`FAULT_BRA_SELF`)**: Opcode fetch is `BRA *` (`$20 $FE`), indicating software panic / abort.
- **Execution Limit Exceeded (`FAULT_MAX_CYCLES` / `FAULT_MAX_TIME` / `FAULT_MAX_WATCHPOINT`)**: The requested `--stop` threshold or watchpoint was reached.
- **Host Interrupt (`FAULT_SIGINT`)**: Tether received `SIGINT` (`^C`) while 6309 CPU was running in Phase 2 or 3, inducing register frame capture and core dump.

#### Core Dump Handling
When any fault or stop limit occurs:
1. Firmware asserts `HALT` to freeze the 6309.
2. Firmware sends `C_FAULT` header followed by 64 chunks of 1024 bytes (`C_CORE_DUMP`), streaming the entire 64KB RAM contents.
3. Tether saves the 64KB binary dump to `/tmp/turbolab-fault-<timestamp>.img` and `/tmp/fault.img`.
4. Inspect the core dump using standard binary tools:
   ```bash
   hexdump -C /tmp/fault.img | head -n 40
   ```
