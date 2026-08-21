# Merging Design Document: TFR911 ↔ Centipede

**Branch:** `work-che-1`
**Date:** 2026-08-21
**Status:** Phase 1 complete (COBS protocol alignment)

---

## 1. Goals

The ultimate goal is to **merge the firmware (C++) and tether (Go) codebases**
of two related but independently-developed circuit boards:

- **TFR911** — a standalone board with an HD63C09EP vintage CPU and an RP2350B
  microcontroller, connected by USB to a PC.
- **Centipede** — a plug-in board for a Radio Shack Color Computer 2 (CoCo2),
  with a MC6809E vintage CPU and an RP2350B, also connected by USB to a PC.

Both boards share the same microcontroller family, the same general architecture
(RP2350B mediates between a 6809 CPU and a USB-connected PC), and very similar
Go tether programs. However, they diverged in protocol design:

- TFR911 used a **raw byte-stream protocol** with inline size-encoding
  (`putsz`/`GetSize`/`PutSize`) and command bytes that implicitly determined
  packet lengths.
- Centipede uses **COBS (Consistent Overhead Byte Stuffing) framing** with
  one's-complement checksums, providing reliable packet boundaries, error
  detection, and a clean separation between framing and payload.

Centipede also has several more-mature subsystems that TFR911 will eventually
adopt: littlefs, Tcl 6.7c, VFS RPC, PicoRPC, and a "pcb" (Proto Crawl Buffer)
serialization scheme for structured RPC messages.

Merging will proceed incrementally, starting with protocol alignment and
working toward shared code.

---

## 2. The Two Worlds

### 2.1 TFR911

| Aspect | Detail |
|--------|--------|
| **Board** | Custom PCB ("TFR911") |
| **Vintage CPU** | HD63C09EP (Hitachi enhanced 6809) |
| **Microcontroller** | RP2350B |
| **Firmware** | `tmanager911/very-turbos/veryturbos.cpp` (~470 lines) |
| **Tether** | `tconsole/` (Go, `package main`) |
| **OS** | TurbOS9 (NitrOS9-derived), Level 1 |
| **Features** | Cycle tracing, CPU simulation (turbo9sim), disk emulation, ROM loading (DECB, SREC), web UI |
| **Architecture** | Core 1 runs `RunCPU()` which clocks the 6809 via PIO, polls USB. Core 0 sleeps. |

The TFR911 firmware is relatively small and focused: it drives the 6809's bus
via PIO state machines, provides RAM, emulates I/O devices (ACIA serial, disk
controller), and communicates with the PC tether for disk images, ROM loading,
and console I/O.

### 2.2 Centipede

| Aspect | Detail |
|--------|--------|
| **Board** | "Copico Centipede" plug-in for CoCo2 |
| **Vintage CPU** | MC6809E (original Motorola) in the CoCo2 |
| **Microcontroller** | RP2350B on plug-in board |
| **Firmware** | `merging/centipede/v1/firmware/centipede.cpp` (~1100 lines) |
| **Tether** | `merging/centipede/v1/tether/` (Go, `package main`) |
| **OS** | NitrOS9 Level 1 / DECB |
| **Features** | COBS protocol, littlefs, Tcl 6.7c REPL, VFS RPC, PicoRPC, pcb encoding, floppy emulation, keyboard injection, coroutines, boot menu, compressed cycle tracing, Orchestra-90 |
| **Architecture** | Core 0 runs USB pipeline (COBS decode, RPC dispatch, Tcl). Core 1 runs the bus-watching "gspoon" foreground. |

The Centipede firmware is significantly more feature-rich. Key subsystems
the TFR911 will eventually adopt:

- **COBS + Checksums** — Reliable USB framing (✅ done for TFR911)
- **littlefs** — On-flash filesystem for persistent storage
- **Tcl 6.7c** — Embedded scripting language for interactive firmware control
- **VFS RPC** — The firmware calls the PC tether to perform host-side
  filesystem operations (open, read, write, stat, readdir, etc.)
- **PicoRPC** — The reverse: the PC tether calls into the firmware
  (ping, exec, flash operations)
- **pcb (Proto Crawl Buffers)** — A simplified Protocol-Buffers-like encoding
  for structured RPC request/response messages, using `(field_number << 3 | wire_type)`
  tags with varint, string, and bytes wire types

---

## 3. Shared Code and Conventions

Code shared between TFR911 and Centipede lives under `merging/centipede/v1/`
and is referenced by relative paths (C++) or Go module `replace` directives.

### 3.1 Shared C++ (via `#include` with relative paths)

| File | Purpose |
|------|---------|
| `merging/centipede/v1/util/cobs.h` | `CobsDecoder` template, `CobsEncodeAndTransmit()`, `CobsChecksum()` |
| `merging/centipede/v1/util/circbuf.h` | `CircBuf<T, N>` lock-free ring buffer |

### 3.2 Shared Go (via `go.mod` replace directive)

| Package | Purpose |
|---------|---------|
| `github.com/strickyak/copico-centipede/v1/tether/cobs` | `Encode()`, `Decode()`, `StreamEncoder()`, `UseChecksums` flag |

The root `go.mod` at `tfr9/` maps this import path to the local copy:

```
replace github.com/strickyak/copico-centipede => ./v3/merging/centipede
```

---

## 4. What We Have Done (Phase 1)

**Commit:** `026df0c` on branch `work-che-1`

### 4.1 Protocol Conversion

Converted TFR911 from raw byte-stream to COBS framing with checksums:

**Firmware (C++) — Outgoing:**
- `ShowChar()`, `putbyte()` → build `[C_PUTCHAR, byte]` packets via `CobsEncodeAndTransmit()`
- `TransmitMessage()` → build `[cmd, payload...]` packets via `CobsEncodeAndTransmit()`
- `TransmitWrite()`, `TransmitCycle()` → same pattern
- Added `cobs_printf()` for formatted multi-byte `C_PUTCHAR` packets
- Replaced all raw `printf()` calls in `veryturbos.cpp`, `turbo9os.h`, `flash-label.h`
- Removed `putsz()`, `TransmitHeader()` (size-encoding no longer needed)

**Firmware (C++) — Incoming:**
- Added `CobsDecoder<1024, 16>` fed from `usb_input` CircBuf
- `PollUsbInput()` runs `cobs_decoder.Tick()` and dispatches decoded packets:
  - `C_PUTCHAR` → keystroke into `term_input`
  - `C_PRE_LOAD` → poke bytes into RAM (for ROM/DECB loading)
  - `C_REBOOT` → `reset_usb_boot()`
  - `C_DISK_READ` → placeholder for disk reply handling

**Tether (Go) — Outgoing:**
- `WriteBytes()` calls `cobs.Encode()` and wraps with `0x00` frame delimiters
- Keystrokes sent as `[C_PUTCHAR, char]` packets
- `C_PRE_LOAD` packets no longer include old size-encoding bytes
- Disk read replies assembled as single `[C_DISK_READ, params..., sector...]` packets

**Tether (Go) — Incoming:**
- COBS decoder goroutine splits raw serial bytes on `0x00` delimiters,
  decodes via `cobs.Decode()`, pushes complete packets to `cobsChan`
- `RunSelect()` reads `[]byte` packets from `cobsChan`
- `C_PUTCHAR` handler loops over all payload bytes (supports multi-byte
  `cobs_printf` output)
- All `GetPacket(fromUSB, cmd)` calls replaced with `packet[1:]`

### 4.2 Build System

- Upgraded `go.mod` from Go 1.18 to Go 1.23
- Added `replace` directive for local `copico-centipede`
- `go build ./v3/tconsole/` succeeds

### 4.3 Verified Working

TurbOS9 boots on TFR911 hardware and responds to commands (`mdir`, `mfree`,
etc.) over the new COBS protocol.

---

## 5. Future Steps

### Phase 2: Dead Code Cleanup

Remove legacy functions that are now unused:
- `getByte()`, `logGetByte()`, `GetPacket()`, `GetSize()`, `PutSize()` in `tconsole.go`
- Old `circbuf.h` in `tmanager911/very-turbos/` (now using Centipede's)

### Phase 3: Add littlefs to TFR911

Centipede uses littlefs for on-flash persistent storage. TFR911 should adopt
the same implementation (`merging/centipede/v1/littlefs/`) to store
configuration, boot images, and user files on the RP2350B's flash.

### Phase 4: Add Tcl 6.7c to TFR911

Centipede embeds a Tcl 6.7c interpreter for interactive firmware control
and scripting. Adding this to TFR911 would provide the same interactive
debugging and configuration capabilities. The Tcl I/O layer
(`tcl_io.h`, `tcl_commands.h`) is tightly integrated with COBS — it reads
input from the COBS packet stream and writes output via `cobs_printf`.

### Phase 5: VFS RPC

Centipede's VFS RPC allows the firmware to call the PC tether to perform
host-side filesystem operations. This is used for:
- Floppy disk emulation over the host filesystem
- File serving to the vintage OS
- Boot image loading

The pcb (Proto Crawl Buffer) encoding is already used by Centipede for
VFS RPC request/response serialization. Adding VFS RPC to TFR911 would
replace the current ad-hoc disk read/write protocol with the same
structured RPC mechanism.

### Phase 6: PicoRPC

The reverse direction: the PC tether calls into the firmware. Centipede
uses this for:
- Ping/health checks
- Executing shell commands on the host
- Flash management

### Phase 7: Unified Tether

With protocols fully aligned, the `tconsole/` (TFR911) and
`merging/centipede/v1/tether/` (Centipede) Go programs should share
most of their code. The goal is a single tether binary that adapts
behavior based on the connected board (detected via flash label metadata
like `b=tfr9` vs `b=centipede`).

### Phase 8: Unified Firmware

The firmware C++ codebases share the same microcontroller and many of the
same abstractions. The differences are primarily in:
- PIO programs (bus timing differs between HD63C09EP and MC6809E)
- Pin assignments (different PCB layouts)
- I/O device emulation (TFR911 emulates its own peripherals; Centipede
  intercepts CoCo2 hardware)

A unified firmware would use compile-time or runtime configuration to
select the appropriate PIO programs and pin maps.

---

## 6. Protocol Reference

### 6.1 Wire Format

All USB communication uses COBS framing:

```
[0x00] [COBS-encoded payload] [0x00]
```

The payload (before COBS encoding) consists of:
```
[command_byte] [payload_data...]
```

With `COBS_CHECKSUMS=1` (default), a one's-complement checksum byte is
appended to the payload before COBS encoding.

### 6.2 Command Bytes (Pico → PC)

| Code | Name | Payload |
|------|------|---------|
| 130–139 | `C_LOGGING` | Logging text at levels 0–9 |
| 164 | `C_RAM_CONFIG` | RAM configuration byte |
| 168 | `C_DUMP_LINE` | 3-byte addr + 16 bytes data |
| 169 | `C_DUMP_STOP` | End of dump |
| 172 | `C_EVENT` | Event data |
| 173 | `C_DISK_READ` | Disk read request |
| 174 | `C_DISK_WRITE` | Disk write notification |
| 193 | `C_PUTCHAR` | One or more display characters |
| 195 | `C_RAM2_WRITE` | addr_hi, addr_lo, data |
| 200 | `C_CYCLE` | 8 bytes: cycle trace data |

### 6.3 Command Bytes (PC → Pico)

| Code | Name | Payload |
|------|------|---------|
| 163 | `C_PRE_LOAD` | addr_hi, addr_lo, data... |
| 173 | `C_DISK_READ` | Disk read reply (params + sector) |
| 192 | `C_REBOOT` | (none) |
| 193 | `C_PUTCHAR` | Single keystroke byte |

---

## 7. Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    PC (Linux)                           │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │  tconsole (Go)                                  │    │
│  │                                                 │    │
│  │  ┌──────────┐   ┌──────────┐   ┌────────────┐  │    │
│  │  │ Terminal  │   │  Disk    │   │  ROM/DECB  │  │    │
│  │  │  I/O     │   │ Emulate  │   │  Loader    │  │    │
│  │  └────┬─────┘   └────┬─────┘   └─────┬──────┘  │    │
│  │       │              │               │          │    │
│  │       └──────────────┼───────────────┘          │    │
│  │                      │                          │    │
│  │              ┌───────┴────────┐                 │    │
│  │              │  COBS Encode/  │                 │    │
│  │              │  Decode Layer  │                 │    │
│  │              │  (cobs pkg)    │                 │    │
│  │              └───────┬────────┘                 │    │
│  └──────────────────────┼──────────────────────────┘    │
│                         │ USB Serial (/dev/ttyACM0)     │
└─────────────────────────┼───────────────────────────────┘
                          │
┌─────────────────────────┼───────────────────────────────┐
│  RP2350B                │                               │
│                         │                               │
│  ┌──────────────────────┴──────────────────────────┐    │
│  │              COBS Encode/Decode                  │    │
│  │       (CobsDecoder + CobsEncodeAndTransmit)     │    │
│  │              from util/cobs.h                    │    │
│  └──────────────────────┬──────────────────────────┘    │
│                         │                               │
│  ┌──────────────────────┴──────────────────────────┐    │
│  │  Firmware (veryturbos.cpp)                      │    │
│  │  Core 1: RunCPU() + PollUsbInput()              │    │
│  │  - PIO bus interface to 6809                    │    │
│  │  - RAM (64KB)                                   │    │
│  │  - I/O device emulation                         │    │
│  │  - ACIA serial (term_input)                     │    │
│  └─────────────────────────────────────────────────┘    │
│                         │ PIO                           │
└─────────────────────────┼───────────────────────────────┘
                          │
                 ┌────────┴────────┐
                 │  HD63C09EP CPU  │
                 │  running        │
                 │  TurbOS9        │
                 └─────────────────┘
```
