# TurboLab Specification

TurboLab is a standalone subset of functionality for the TFR/911h 6309E Single Board Computer.

The TurboLab configuration has its own firmware (`turbolab/firmware/`) and its own tether program (`turbolab/tether/`), completely independent of and not necessarily compatible with the "normal" v4 firmware and tether.

## Architecture Principles

* **Tethered operation only**: The board does not need to function without the tether.
* **On the Pico**: No TCL. No LittleFS. Core 0 handles USB CDC-ACM communications and host RPCs; Core 1 handles the 6309 bus interface and trace capture via Hamster PIO.
* **On the PC**: No webserver. Only Level 1 OS-9.
* **Trace and packet logging to `stderr`**: Console stdout is reserved for terminal output from the 6309 system.

---

## Supported Tether Flags

* `--exit`  
  Immediately exit(0) without connecting to serial or executing any logic. Tested first and takes precedence over all other flags; enables shell scripts to probe which tether executable runs on the host PC architecture.

* `--wire=/dev/ttyACM0`  
  Serial device connected by USB to Pi Pico (default `/dev/ttyACM0`).

* `--baud=115200`  
  Serial device baud rate (default `115200`).

* `--debug=u`  
  Debug options: `u` enables logging of raw incoming and outgoing COBS packets on `stderr` (`PACKET IN` and `PACKET OUT`).

* `--trace=x,+,r,w,i,t,1`  
  Comma-separated trace flags:
  * `1`: Shortcut to turn on all trace options (`x,+,r,w,i,t`).
  * `x`: Opcode execution (first instruction byte / FIC cycles only).
  * `+`: Subsequent opcode/operand bytes (following PC).
  * `r`: Read cycles; automatically implies `r,x,i,+` (all read/fetch cycles).
  * `w`: Data write cycles.
  * `i`: Interrupt and RTI events.
  * `t`: OS-9 Traps (API call and return).

* `--trigger=c:50k`, `--trigger=s:5`, or `--trigger=[r|w|x:]<addr>[:N]`  
  Trigger trace after cycle count (e.g. `c:50k`), duration in seconds (e.g. `s:5`), or watchpoint event:
  * Event types: `r:` (Nth read), `w:` (Nth write), `x:` (Nth opcode fetch with FIC).
  * Address formats: numeric literal (`0x1234`, `$1234`, or decimal), or module-relative `@modulename+offset` (or `@modulename` with offset 0). The offset can be decimal or hexadecimal (`$10`, `0x10`, `16`).
  * Shorthand: `@modulename[+offset][:N]` defaults to execution watchpoint `x:`.
  * Count suffixes: `k=1000`, `m=1000000`, `g=1000000000` (decimal SI) and `K=1024`, `M=1048576`, `G=1073741824` (binary).
  * Trace output prior to trigger is suppressed.

* `--max=c:1m`, `--max=t:30s`, or `--max=[r|w|x:]<addr>[:N]`  
  Maximum execution limit by cycle count (e.g. `c:1m`, `c:64K`), duration (e.g. `t:30s`), or watchpoint event (`r:`, `w:`, `x:`, or `@modulename[+offset][:N]`). Reaching the limit triggers a clean stop and core dump.

* `--watch=[r|w|x:]addr1,[r|w|x:]addr2,...`  
  Comma-separated list of Logging Watchpoints that print matching bus cycles to stderr, even when general tracing (`--trace`) is disabled or before a trigger condition is met:
  * Targets can be numeric literals (`0x0020`, `$0020`, decimal) or symbolic module offsets (`@kernel+0x1B`).
  * Match cycle kinds: default is any cycle (`r`, `w`, or `x`), or specify `r:addr`, `w:addr`, `x:addr`.
  * Optional Nth count filter (`addr:N`): fire only on the Nth matching access.

* `--listings=listings_dirname`  
  Directory containing pre-assembled OS-9 module listings named `<name>.<size><crc>` (e.g. `kernel.0d4eec829c`, `ioman.070aaecac4`). Primordial modules identified in the RAM image that lack a command-line listing are automatically resolved from this directory.

* `--reflash`  
  Reboot the Pico into RP2350 BOOTSEL mode for firmware reflashing and exit cleanly.

---

## Command Line Arguments

Arguments following the flags specify the memory image and listings:

```bash
tether [flags] image_file.img | module_files... [listings.list...]
```

* `image_file.img`: Exactly 65536 bytes; ends with the 6309 RESET vector at `$FFFE-$FFFF`.
* `module_files`: Raw OS-9 binary module file(s) to be assembled into the memory image. Modules are placed contiguous downward starting just below the I/O vector area (`$FF00`).
* **Kernel & Reset Vector Resolution**: When raw module files are loaded, Tether scans the modules for one named `kernel` or `krn` (case-insensitive) and sets the 6309 RESET vector at `$FFFE-$FFFF` to that module's execution entry point (`BaseAddr + 9`). If neither is found, it falls back to the entry point of the first loaded module.
---

## Triggers, Limits, and Watchpoints (`--trigger` and `--max`)

The `--trigger` flag delays trace streaming until a specific condition occurs.  
The `--max` flag sets an automatic termination threshold that freezes the CPU and produces a full 64KB core dump.

### 1. Cycle Limits (`c:<count>`)
Limits execution to a specified number of 6309 clock cycles:
* **Decimal SI suffixes (powers of 10)**:
  * `k` = $1,000$ ($10^3$)
  * `m` = $1,000,000$ ($10^6$)
  * `g` = $1,000,000,000$ ($10^9$)
* **Binary suffixes (powers of 2, "big" letters for "big" units)**:
  * `K` = $1,024$ ($2^{10}$)
  * `M` = $1,048,576$ ($2^{20}$)
  * `G` = $1,073,741,824$ ($2^{30}$)
* Numeric counts can also be specified in hex (e.g. `c:$1000` or `c:0x1000`).
* Examples:
  * `--max=c:50k`: Stop after exactly 50,000 cycles.
  * `--max=c:64K`: Stop after 65,536 cycles.
  * `--trigger=c:1M`: Begin tracing after 1,048,576 cycles.

### 2. Time Limits (`s:<duration>` or `t:<duration>`)
* `--trigger=s:<duration>`: Delay trace output by wall-clock time (e.g. `s:5`, `s:2.5s`).
* `--max=t:<duration>`: Terminate after wall-clock duration (e.g. `t:500ms`, `t:10s`, `t:1m`).

### 3. Memory & Module Watchpoints
Hardware watchpoints monitor bus transactions on Core 1 in real time and trigger on matching cycles:

| Prefix | Name | Bus Match Condition |
|---|---|---|
| `x:` | **Execution** | Memory read with First Instruction Cycle (`FIC=1`) matching address |
| `r:` | **Read** | Any memory read cycle (opcode fetch, operand, or data load) matching address |
| `w:` | **Write** | Any memory write cycle (`STA`, `STX`, stack pushes, etc.) matching address |

#### Target Address Formats:
* **Hexadecimal literal**: `$1234` or `0x1234`
* **Decimal literal**: `4096`
* **Module-relative symbol**: `@modulename+offset`
  * Resolves against primordial OS-9 modules scanned from the loaded image (e.g. `kernel`, `init`, `tk`, `ioman`, `scf`, `shell`).
  * Module names are matched case-insensitively (`@kernel`, `@Kernel`, `@KERNEL`).
  * Offset can be **decimal** (`+16`, `+27`) or **hexadecimal** (`+$10`, `+$1B`, `+0x10`, `+0x1B`, `+1a`).
  * Omitting the offset (e.g. `@kernel` or `@shell`) defaults to offset `+0`.
  * Negative offsets are supported (e.g. `@kernel-2`).

#### Shorthand & Event Counts:
* **Execution Shorthand**: Specifying `@modulename[+offset]` without a prefix defaults to execution watchpoint `x:` (e.g. `--max=@kernel+0x1B` is shorthand for `--max=x:@kernel+0x1B:1`).
* **Nth Occurrence (`:N`)**: Appending `:N` triggers on the Nth occurrence of the event (e.g. `w:0x0020:5` stops on the 5th write to `$0020`).

#### Watchpoint Examples:
```bash
# Stop on the 1st instruction of the kernel entry point ($D4F9):
tether -n --max=@kernel+0x1B turbos_dev.img

# Stop on the 10th execution of the kernel entry point:
tether -n --max=@kernel+0x1B:10 turbos_dev.img

# Stop on the 5th write to DP location $0020:
tether -n --max=w:0x0020:5 turbos_dev.img

# Start tracing when the shell module begins executing:
tether --trigger=@shell turbos_dev.img
```

### 4. Logging Watchpoints (`--watch`)

The `--watch` flag sets one or more addresses as "Logging Watchpoints". When a matching cycle occurs on the bus, that cycle is emitted and printed to stderr, even when general tracing (`--trace`) is disabled, or before the trigger condition (`--trigger`) has fired:

* **Cycle Types**:
  * Default (`addr`): Match any bus cycle touching `addr` (reads, writes, and instruction fetches).
  * `r:addr`: Match only read cycles.
  * `w:addr`: Match only write cycles.
  * `x:addr`: Match only opcode fetch cycles (`FIC=1`).
* **Address Expressions**:
  * Hexadecimal literal: `$0020`, `0x0020`
  * Decimal literal: `32`
  * OS-9 module offset: `@kernel+0x1B`, `@shell`
* **Nth Count (`addr:N`)**:
  * Fire only on the Nth occurrence (e.g. `0x1019:3` logs only the 3rd access).
* **Firmware Implementation**:
  * Guarded by `#if ENABLE_WATCHPOINTS`.
  * Logging Watchpoints are linked together before Trigger and Max watchpoints in a chain.
  * Called via a high-performance function pointer (`wp_hook`) in the tight CPU loop; when no watchpoints are active, `wp_hook` is `nullptr` and skipped with a single null-pointer test.

```bash
# Log every access to zero-page address $0020 without tracing:
tether -n --watch=0x0020 --max=c:50k turbos_dev.img

# Watch writes to $0020 and $0021, and trace instructions when @kernel+0x1B is hit:
tether --watch=w:0x0020,w:0x0021 --trigger=@kernel+0x1B --trace=x turbos_dev.img
```

---

## Firmware LED Behavior

* **RESTARTED Mode (`STATE_WAIT_CONFIG`)**:  
  Flashes the board LED at **2 Hz with a 50% duty cycle** (250 ms ON, 250 ms OFF) while waiting for tether connection and configuration.

* **Running Mode (`STATE_RUNNING`)**:  
  * Turns **ON** upon an **Interrupt Acknowledge** (CPU reads vector address in range `$FFF0`..`$FFFF`).
  * Turns **OFF** upon an **RTI** (Return from Interrupt) instruction fetch (`reading && kind == KIND_FIC && value == 0x3B && Address < 0xFFF0`).

---

## Pico Restart & Tether Handshake

1. **Startup Beacon & Timeout**:
   * On boot or after a restart, the Pico enters `STATE_WAIT_CONFIG` and immediately begins emitting `C_RESTARTED` (cmd 194) beacon packets.
   * When Tether starts up, it listens on USB for up to **2 seconds** for the `C_RESTARTED` beacon.
   * If `C_RESTARTED` is not received within 2 seconds, Tether sends a `RESTART_NOW_PLEASE` (cmd 177) packet to the Pico, and repeats every 2 seconds until `C_RESTARTED` is received.

2. **Immediate Firmware Reset (`RESTART_NOW_PLEASE`)**:
   * The Pico listens for `RESTART_NOW_PLEASE` in all operating states (`STATE_WAIT_CONFIG`, `STATE_RUNNING`, `STATE_HALTED`).
   * Upon receiving `RESTART_NOW_PLEASE`, the Pico immediately resets without disconnecting the USB CDC-ACM link:
     * Asserts 6309 `RESET` and `HALT`, releases `IRQ`.
     * Stops Core 1 execution (`multicore_reset_core1`).
     * Cancels the 60 Hz repeating timer.
     * Disables and resets Hamster PIO.
     * Clears all cross-core FIFOs and input buffers.
     * Resets ACIA registers, fault flags, triggers, and execution limits.
     * Resets state to `STATE_WAIT_CONFIG`, restarts the 2 Hz LED flash, and sends `C_RESTARTED`.

3. **Configuration & Upload**:
   * Once `C_RESTARTED` is received, Tether issues the `config` RPC with trace flags, triggers, and max execution limits.
   * Tether uploads the 65536-byte RAM image to the Pico in 1024-byte chunks via `upload` RPCs.
   * Tether issues the `start` RPC to release 6309 `RESET` and begin execution.

4. **Firmware Reflash (`T_REFLASH_NOW_PLEASE` / `--reflash`)**:
   * Flag `--reflash` connects to the Pico via USB and awaits the `C_RESTARTED` beacon.
   * Instead of sending configuration and image upload, Tether transmits `T_REFLASH_NOW_PLEASE` (cmd 175) to the Pico.
   * Tether announces the command on stdout and stderr, sleeps 1 second, closes the USB connection, restores the terminal state, and exits 0.
   * The Pico listens for `T_REFLASH_NOW_PLEASE` in all states, safely halts Core 1 and 6309 pins, and triggers `reset_usb_boot(0, 0)` into the RP2350 USB mass-storage BOOTSEL mode.

---

## OS-9 Module Listings & Relocation

* **Primordial Module Scanning**:
  * Tether scans the 64KB RAM image for valid OS-9 module headers (`$87 $CD` sync bytes, header parity, size, name offset, and 24-bit CRC).
  * Discovered modules are recorded with their `BaseAddr`, `Size`, and 24-bit `CRC`.

* **Listing Relocation**:
  * Assembly listings that define an OS-9 module (`MOD` directive at `$0000` with `$87 $CD` sync bytes and `EMOD` CRC) are recognized as relocatable modules.
  * Tether matches each OS-9 module listing to a primordial module found in RAM (by CRC and size).
  * If matched, all instruction and symbol line addresses in the listing are relocated by adding `BaseAddr`.
  * If an OS-9 module listing does not match any module in the RAM image (e.g. alternative builds), it is skipped to avoid polluting low memory (`$0000`..`$0FFF`).
  * Absolute listings (no module header at `$0000`) are loaded as-is without relocation.

* **Directory Lookup (`--listings`)**:
  * Primordial modules in RAM that were not matched by command-line `.list` files are searched in the directory specified by `--listings`.
  * The filename format looked up is: `"%s.%04x%06x"` (lowercase module name, 4-hex-digit size, 6-hex-digit CRC), e.g. `kernel.0d4eec829c`, `ioman.070aaecac4`.
  * If found, the listing is parsed and automatically relocated to the module's `BaseAddr` in RAM.

---

## Output Tracing Format (to `stderr`)

Each traced bus cycle is printed on `stderr`, cleanly separated from 6309 console output on `stdout`.

### Cycle Counter & Reset Synchronization
* The cycle counter begins at `#1` when the 6309 CPU reads the reset vector high byte at address `$FFFE` with the `BS` (Bus Status) bit asserted (`BS=1`).
* **Cycle `#1`**: High byte fetch of reset vector (`r FFFE <hi> #1;s reset vector (high)`).
* **Cycle `#2`**: Low byte fetch of reset vector (`r FFFF <lo> #2;s reset vector (low)`).
* **Cycle `#3`**: Internal 6309 CPU cycle (not traced).
* **Cycle `#4`**: First Instruction Cycle (FIC) at the kernel entry address (`x <addr> <op> #4; ...`).
* Pre-reset cycles (internal 6309 cycles while coming out of reset) are suppressed from trace output, and `cycles` remains `0` until `$FFFE` is read with `BS=1`.

### Lossless Trace Flow Control
* Traced cycles are pushed from Core 1 to Core 0 across the 8192-entry lock-free `fg2bg_trace` FIFO.
* When tracing is active, if the FIFO becomes full due to USB throughput limits, Core 1 pauses before issuing the next bus cycle to Hamster PIO.
* Because the 6309E CPU is a static CMOS processor and Hamster PIO holds clocks E and Q low (`side 0`) between cycles, the CPU safely holds state until Core 0 drains records over USB. This guarantees 100% lossless cycle capture without dropped records, and ensures the trace log runs precisely to the specified `--max c:N` limit.

### Cycle Speed Estimation at Shutdown
* When any tracing is enabled (`--trace` with any flag), Tether samples a kernel timestamp (`time.Now()`) upon receiving the first cycle report, and records the latest cycle number received.
* When Tether shuts down (due to user `^C` / `SIGINT`, limits `--max`, or termination), Tether takes a shutdown kernel timestamp, computes total cycles as `lastCycle - firstCycle`, and divides by elapsed time to estimate execution cycles per second:
  $$\text{CPS} = \frac{\text{lastCycle} - \text{firstCycle}}{\text{elapsed seconds}}$$
* Printed on both `stderr` (logged in `_log`) and terminal stdout as Tether exits:
  `Estimated cycles per second: 180154 (0.180 MHz) [97406 cycles in 0.54s]`

### CPU Status Signals
The four high bits of the trace `kind` byte report live CPU bus and status signals:
* `a` (`0x10`): **BA** (Bus Available)
* `s` (`0x20`): **BS** (Bus Status — sampled from RP2350 GPIO 28)
* `_` (`0x40`): **LIC** (Last Instruction Cycle — sampled from RP2350 GPIO 26)
* `y` (`0x80`): **BUSY** (Hitachi 6309 internal pipeline busy signal)

### Semicolon Formatting
Signal characters appear immediately after the cycle number and semicolon `;` without intervening whitespace:
* `+ D4F4 20 #6;_` (LIC asserted on the last cycle of an instruction; no comment).
* `r FFFE D4 #1;s reset vector (high)` (BS asserted during vector read; followed by space and comment).
* `x D4F2 8E #4; "kernel.0d4eec829c"+0014   ldx #D.FMBM...` (no signals; legacy `; ` space preserved).

### Cycle Kind Prefixes
* `- ---- -- #123000; `  
  Idle cycle (internal CPU operation without bus transfer, identified by AVMA low delayed 1 cycle; address and data lines float and are formatted as `----` and `--`; enabled via `--trace=-` or `--trace=idle`; `--trace=1` implies all flags except idle, so idle cycles require `--trace=1,-` or `--trace=1,idle`).
* `x 80A1 7E #123001; ["<module>"+<offset> ]%s`  
  Opcode fetch (FIC / First Instruction Cycle) at address `$80A1` with opcode byte `$7E`. If the address falls within an OS-9 module, prefixes with lowercase module name, version string, and 4-digit hex offset (e.g., `"krn.0123896745"+0155 `), followed by the disassembled source line `%s`. Lines that do not begin with a label are indented with two extra spaces.
* `+ 80A2 00 #123002; `  
  Additional instruction byte (following PC, operand/immediate byte).
* `r 00F0 7E #123003; `  
  Data read cycle (non-instruction memory or hardware register read). Annotated automatically with `reset vector (high)` / `reset vector (low)` for `$FFFE` / `$FFFF`.
* `w 00F1 7E #123004; `  
  Data write cycle.
* `i IRQ     #123005; ` / `i RTI     #123050; `  
  Interrupt vector access or Return from Interrupt (`RTI`).
* `t SWI2    #123100; `  
  OS-9 system call trap (`SWI2`).

---

## Console & Terminal Interaction

* **Cooked Mode & Readline**:
  * Tether keeps the host terminal in cooked mode (does not put tty into raw/cbreak mode).
  * Tether provides line editing with history support (arrow keys, `^A`, `^E`, `^U`, `^K`).
  * Entering a line with `^C` or `^c` sends an interrupt character (`0x03`) to the 6309 console ACIA.
* **Bidirectional Console**:
  * Keyboard input lines are queued and sent via `T_TERM_CHARS` (cmd 178) to the Pico.
  * 6309 console output bytes are sent via `C_PICO_CHARS` (cmd 201) to Tether and printed to stdout.

---

## Hardware Protections, Faults & Core Dumps

* **Supported I/O Ports**:
  * Only ports `$FF00`..`$FF03` of the Turbo9Sim / 6850 ACIA specification are supported:
    * `$FF00`: ACIA Status register (read-only; transmit ready bit 1, receive ready bit 0).
    * `$FF01`: ACIA Data register (read/write).
    * `$FF02`: Timer Status / IRQ ACK register (read/write; bit 0 = 60Hz tick interrupt).
    * `$FF03`: Reserved.
* **Startup Write Suppression & R_W Pull-Up**:
  * During the initial 6309 reset sequence (prior to and during the `$FFFE-$FFFF` vector fetch), the CPU drives dummy or floating states on the bus, often with `$FFFF` on the address bus and a low/glitchy `R_W`.
  * **Electrical Pull-Up**: The RP2350 internal pull-up is enabled on `R_W` (GPIO 31) via `gpio_pull_up(R_W)` to prevent the floating line from drifting low.
  * **Write Suppression**: All writes to RAM and emulated peripherals are strictly disabled until the RESET vector fetch completes (`$FFFF` read following `$FFFE` with `BS=1`). This prevents dummy reset cycles from corrupting memory or the reset vector (`$FFFF`).
  * **Post-Reset Vector Writability**: Once the reset vector fetch completes (`reset_achieved`), writes are permitted anywhere in RAM, including the vector table (`$FFF0..$FFFF`), ensuring that software can freely install or relocate exception and interrupt handlers in RAM.
* **Fault Invalidation & Reset Window**:
  * To prevent spurious aborts, **Red Page (`FAULT_RED_PAGE`)** and **Zero Vector (`FAULT_ZERO_VECTOR`)** checks are inhibited until the reset vector fetch completes (`$FFFF` read following `$FFFE` with `BS=1`).
  * If the CPU fails to fetch the reset vector within 100,000 cycles (~61 ms) of HALT release, a watchdog timeout aborts with `FAULT_ZERO_VECTOR`.
* **Red-Paged I/O Abort**:
  * The rest of the I/O page (`$FF04`..`$FFEF`) is **Red-Paged**. Any read or write access to this region once reset is achieved triggers an immediate CPU abort (`FAULT_RED_PAGE`).
* **Zero Interrupt Vector Abort**:
  * When an interrupt acknowledge occurs (`BS=1`) and the fetched vector byte is `$00`, an immediate abort is triggered (`FAULT_ZERO_VECTOR`).
* **Infinite Self-Branch Panic Abort**:
  * When an opcode fetch (FIC) is `BRA` (`$20`) with relative offset `$FE` (i.e. `BRA *` / `BRA .`), representing an infinite loop commonly used for software panic or abort conditions, an immediate abort is triggered (`FAULT_BRA_SELF`).
* **Limit Exceeded Abort**:
  * Reaching the configured `--max=c:...`, `--max=t:...`, or `--max=r/w/x:...` limit triggers `FAULT_MAX_CYCLES`, `FAULT_MAX_TIME`, or `FAULT_MAX_WATCHPOINT`.
* **Automatic 64KB Core Dump**:
  * Upon any abort, the CPU is immediately halted and Core 1 reads the full 64KB RAM image.
  * The Pico streams the memory image to Tether in 64 chunks of 1024 bytes via `C_CORE_DUMP` (cmd 203).
  * Tether reconstructs the image and writes it to `/tmp/turbolab-fault-<timestamp>.img` and `/tmp/fault.img`.

---

## Packet Protocol Reference

All packets across the USB CDC-ACM serial link are framed using **COBS** (Consistent Overhead Byte Stuffing) with zero-byte delimiters.

| Command ID | Hex | Direction | Name | Description |
|---|---|---|---|---|
| 175 | `$AF` | Host -> Pico | `T_REFLASH_NOW_PLEASE` | Force reboot into RP2350 USB BOOTSEL mode (one-way, no RPC reply) |
| 177 | `$B1` | Host -> Pico | `T_RESTART_NOW_PLEASE` | Force immediate firmware and CPU reset (one-way, no RPC reply) |
| 178 | `$B2` | Host -> Pico | `T_TERM_CHARS` | Send terminal keyboard input characters to ACIA |
| 181 | `$B5` | Bidirectional | `T_PICO_RPC` | RPC request/response frame (`config`, `upload`, `start`, `ping`) |
| 194 | `$C2` | Pico -> Host | `C_RESTARTED` | Startup/reset beacon packet |
| 200 | `$C8` | Pico -> Host | `C_TRACE_CYCLES` | Batched bus cycle trace buffer (bundles 12-byte `TraceRecord` entries) |
| 201 | `$C9` | Pico -> Host | `C_PICO_CHARS` | Terminal console output characters from ACIA |
| 202 | `$CA` | Pico -> Host | `C_FAULT` | Fault notification (reason code, cycle, address, data) |
| 203 | `$CB` | Pico -> Host | `C_CORE_DUMP` | 1KB memory chunk of post-fault core dump |

### Trace Record Structure (`C_TRACE_CYCLES`)

Trace packets bundle up to 20 fixed-size 12-byte binary records:

```text
Offset  Type      Field   Description
──────  ────────  ──────  ──────────────────────────────────────────────────────────
 0..7   uint64_t  cycle   Cycle counter (little-endian), starts at 1 on $FFFE fetch
 8..9   uint16_t  addr    16-bit address on address bus (little-endian)
  10    uint8_t   data    8-bit data value on data bus
  11    uint8_t   kind    Packed cycle kind (low nybble) and CPU signals (high nybble)
```

#### `kind` Byte Bit Allocations:
* **Bits 0..3 (Cycle Kind)**:
  * `0`: `KIND_IDLE` (`-`) — Internal/idle cycle (identified by AVMA low delayed 1 cycle; printed as `- ---- -- #<cycle>`; enabled via `--trace=-` or `--trace=idle`)
  * `1`: `KIND_FIC` (`x`) — First Instruction Cycle (including `RTI` opcode fetch `$3B`)
  * `2`: `KIND_OPCODE_CONT` (`+`) — Subsequent opcode/operand byte
  * `3`: `KIND_READ` (`r`) — Data read cycle (including interrupt vector fetches at `$FFF0`..`$FFFD`)
  * `4`: `KIND_WRITE` (`w`) — Data write cycle
  * Note: Flag `--trace=1` (or `all`) implies all trace flags (`x`, `+`, `r`, `w`, `i`, `t`) *except* idle (`-`). To include idle cycles, explicitly specify `--trace=1,-` or `--trace=1,idle`.
  * Note: Flag `--trace=i` enables emission of interrupt vector reads (`is_bs`) and `RTI` instruction fetches (`is_rti`) with their natural `r` and `x` tags. Tether identifies the interrupt context from the `s` (`FLAG_BS`) signal and vector addresses, annotating them with their vector names (e.g. `IRQ vector (high)`).
* **Bits 4..7 (CPU Status Flags)**:
  * Bit 4 (`0x10`): `a` = `FLAG_BA` (Bus Available)
  * Bit 5 (`0x20`): `s` = `FLAG_BS` (Bus Status — GPIO 28)
  * Bit 6 (`0x40`): `_` = `FLAG_LIC` (Last Instruction Cycle — GPIO 26)
  * Bit 7 (`0x80`): `y` = `FLAG_BUSY` (Hitachi 6309 Busy signal)

---

## Multi-Architecture Tether Binaries

All Tether binaries are statically linked with `CGO_ENABLED=0` and built across targets via `make TETHERS`:

* `build/tether.linux-amd64.exe` (Linux x86_64)
* `build/tether.linux-386.exe` (Linux i386)
* `build/tether.linux-arm64.exe` (Linux ARM64 / AArch64)
* `build/tether.linux-arm-7.exe` (Linux ARMv7)
* `build/tether.win-amd64.exe` (Windows x86_64)
* `build/tether.win-386.exe` (Windows i386)
* `build/tether.mac-arm64.exe` (macOS Apple Silicon)
* `build/tether.mac-amd64.exe` (macOS Intel)
