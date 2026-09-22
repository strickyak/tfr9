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

* `--trigger=c:50000` or `--trigger=s:5`  
  Trigger trace after cycle count (e.g. `c:50000`) or duration in seconds (e.g. `s:5`), suppressing trace output prior to the trigger.

* `--max=c:1m` or `--max=t:30s`  
  Maximum execution limit by cycle count (e.g. `c:1m` = 1,000,000 cycles) or time (e.g. `t:30s` = 30 seconds). Reaching the limit triggers a fault and core dump.

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
* `module_files`: Raw OS-9 binary module file(s) to be assembled into the memory image.
* `*.list`: Assembly listings (e.g., `lwasm` output). May be an absolute listing or an OS-9 module listing.

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

Each traced bus cycle is printed on `stderr`:

* `-         #123000; `  
  Idle cycle (no bus access, with cycle counter).
* `x 80A1 7E #123001; ["<module>"+<offset> ]%s`  
  Opcode fetch (FIC / First Instruction Cycle) at address `$80A1` with opcode byte `$7E`. If the address falls within an OS-9 module, prefixes with lowercase module name, version string, and 4-digit hex offset (e.g., `"krn.0123896745"+0155 `), followed by the disassembled source line `%s`. Lines that do not begin with a label are indented with two extra spaces.
* `+ 80A2 00 #123002; `  
  Additional instruction byte (following PC, operand/immediate byte).
* `r 00F0 7E #123003; `  
  Data read cycle (non-instruction memory or hardware register read).
* `w 00F1 7E #123004; `  
  Data write cycle.

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
* **Red-Paged I/O Abort**:
  * The rest of the I/O page (`$FF04`..`$FFEF`) is **Red-Paged**. Any read or write access to this region triggers an immediate CPU abort (`FAULT_RED_PAGE`).
* **Zero Interrupt Vector Abort**:
  * Reading `$0000` as the target of an interrupt vector triggers an immediate abort (`FAULT_ZERO_VECTOR`).
* **Limit Exceeded Abort**:
  * Reaching the configured `--max=c:...` or `--max=t:...` limit triggers `FAULT_MAX_CYCLES` or `FAULT_MAX_TIME`.
* **Automatic 64KB Core Dump**:
  * Upon any abort, the CPU is immediately halted and Core 1 reads the full 64KB RAM image.
  * The Pico streams the memory image to Tether in 64 chunks of 1024 bytes via `C_CORE_DUMP` (cmd 203).
  * Tether reconstructs the image and writes it to `/tmp/turbolab-fault-<timestamp>.img` and `/tmp/fault.img`.

---

## Packet Protocol Reference

All packets across the USB CDC-ACM serial link are framed using **COBS** (Consistent Overhead Byte Stuffing) with zero-byte delimiters.

| Command ID | Hex | Direction | Name | Description |
|---|---|---|---|---|
| 177 | `$B1` | Host -> Pico | `T_RESTART_NOW_PLEASE` | Force immediate firmware and CPU reset (one-way, no RPC reply) |
| 178 | `$B2` | Host -> Pico | `T_TERM_CHARS` | Send terminal keyboard input characters to ACIA |
| 181 | `$B5` | Bidirectional | `T_PICO_RPC` | RPC request/response frame (`config`, `upload`, `start`, `ping`) |
| 194 | `$C2` | Pico -> Host | `C_RESTARTED` | Startup/reset beacon packet |
| 200 | `$C8` | Pico -> Host | `C_TRACE_CYCLES` | Batched bus cycle trace buffer |
| 201 | `$C9` | Pico -> Host | `C_PICO_CHARS` | Terminal console output characters from ACIA |
| 202 | `$CA` | Pico -> Host | `C_FAULT` | Fault notification (reason code, cycle, address, data) |
| 203 | `$CB` | Pico -> Host | `C_CORE_DUMP` | 1KB memory chunk of post-fault core dump |
