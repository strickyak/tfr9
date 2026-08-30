---
name: v4-firmware-architecture
description: >
  Architecture guide for the v4 unified firmware framework that merges the
  TFR911H and Centipede RP2350 firmwares. Covers the CRTP mixin pattern,
  IN_RAM / FORCE_INLINE constraints, the foreground/background dual-core
  model, open-drain GPIO, cross-core FIFOs, and the include-not-rewrite
  strategy for porting v1 code into v4.
---

# v4 Firmware Architecture — Unified TFR911 + Centipede Framework

## 1. What Are We Building?

Two different RP2350-based boards run 6809 CPUs on a real CoCo bus:

| Board | CPU Clock Source | Bus Interface | Key Difference |
|-------|-----------------|---------------|----------------|
| **TFR911H** | PIO-generated E/Q clocks | PIO state machine reads/writes directly | The Pico *is* the memory/device system |
| **Centipede** | CoCo2's own crystal | "Gerbil" PIO wheel monitors bus passively | The Pico *observes and injects* on an existing bus |

Both need the same emulated devices (floppy, console, 64K RAM, Disk Basic ROM,
Tcl shell, LittleFS VFS) but differ in how they physically interact with the bus.

**The v4 framework** unifies both into a single codebase using C++ CRTP
(Curiously Recurring Template Pattern) mixins, so shared device logic is
written once and the hardware-specific bus interface is pluggable.

## 2. Directory Layout

```
v3/merging/centipede/
├── v1/                          # Original firmware (working, deployed)
│   ├── firmware/
│   │   ├── centipede.cpp        # Monolithic v1 build
│   │   ├── floppy.h             # DoFloppy<T> / DontFloppy<T>  (CRTP)
│   │   ├── coco64k.h            # DoCoco64k<T> / DontCoco64k<T>
│   │   ├── gspoon.h             # Tcl console / spoonfeeder
│   │   ├── console.h            # VDG display emulation
│   │   ├── cross-core.h         # Lock-free FIFO (CrossCoreFIFO<T,N>)
│   │   ├── coro.h               # Cooperative coroutines
│   │   └── ...                  # ~30 more headers
│   ├── util/
│   │   ├── cobs.h               # COBS encode/decode + checksums
│   │   └── circbuf.h            # Lock-free circular buffer
│   ├── tcl6.7c/                 # Tcl 6.7c interpreter (C)
│   ├── littlefs/                # LittleFS filesystem (C)
│   └── miniz/                   # zlib-compatible compression (C)
│
├── v4/                          # Unified v4 framework
│   ├── v4_centipede_main.cpp    # Centipede build (compiles, produces .uf2)
│   ├── v4_tfr911_main.cpp       # TFR911 build (stub)
│   ├── v4_types.h               # Shared types, macros, enums
│   ├── v4_engine_centipede.h    # CentipedeEngine<T> CRTP mixin
│   ├── v4_engine_tfr911.h       # TFR911Engine<T> CRTP mixin
│   ├── v4_core_engine.h         # CoreEngine<T> shared base
│   ├── v4_floppy.h              # DoFloppy/DontFloppy (v4 version)
│   ├── v4_compat_centipede.h    # Shim: maps v4 names to v1 expectations
│   ├── CMakeLists.txt           # Pico SDK build config
│   └── ...
```

## 3. The CRTP Mixin Pattern

Every emulated device is a `template <typename T> struct DoSomething` or
`DontSomething`. The final `Engine` struct inherits from the desired set:

```cpp
class Engine : public DoFloppy<Engine>,
               public DoCoco64k<Engine>,
               public V4CoreEngine<Engine> {
 public:
  static void RunEngine() {
    InitCoco64k();
    RunCores(core1_trampoline, core0_trampoline);
  }
};
```

**Key rules for CRTP in this codebase:**

1. **All methods are `static`** — no `this` pointer, no virtual dispatch,
   no vtable. The 6809 bus cycle loop runs at 250 MHz and cannot afford
   any indirection.

2. **No non-static member variables** — use file-scope globals (which live
   in BSS/RAM) or `static inline` class members. This ensures the
   foreground loop accesses only RAM, never flash-allocated vtable pointers.

3. **`T::Method()` calls resolve at compile time** — the compiler sees
   through the CRTP and inlines everything. A `DontFloppy<T>::ReadScsFloppy`
   that returns `0xFF` compiles to literally nothing.

4. **`Do*` vs `Dont*` mixins** — `DontFloppy<T>` has empty no-op methods;
   `DoFloppy<T>` has the real implementation. Swap one for the other in the
   Engine inheritance list to enable/disable a feature at compile time with
   zero runtime cost.

## 4. The IN_RAM Constraint (Critical!)

### Why IN_RAM Matters

The RP2350 executes code from external QSPI flash via XIP (execute-in-place)
with a small cache. A cache miss causes a **multi-microsecond stall**. On the
Centipede, the Gerbil PIO state machine delivers bus cycles at ~1 MHz — a
single flash stall means **missing a bus cycle and corrupting data**.

### The GCC Limitation

GCC does **not** support `__attribute__((section(...)))` on class methods.
This means you **cannot** mark a class method as IN_RAM:

```cpp
// THIS DOES NOT WORK — GCC silently ignores the attribute!
struct Engine {
  static void IN_RAM foreground() { ... }  // ❌ Still in flash!
};
```

### The Solution: Free Templated Functions

The foreground and background loops must be **free functions** (not methods):

```cpp
// ✅ This works — free function carries the IN_RAM attribute
template <typename T>
void IN_RAM centipede_foreground_loop() {
  while (true) {
    T::ReadRam(addr);      // FORCE_INLINE → compiled into this function
    T::WriteIO(addr, val); // FORCE_INLINE → compiled into this function
  }
}

// Trampoline (also a free IN_RAM function)
void IN_RAM core1_trampoline() {
  centipede_foreground_loop<Engine>();
}
```

### FORCE_INLINE

Every `T::Method()` called from the IN_RAM loop **must** be `FORCE_INLINE`:

```cpp
template <typename T>
struct DoFloppy {
  FORCE_INLINE static void ReadScsFloppy(const uint& abus, byte& dbus) {
    // This entire body gets inlined into the IN_RAM foreground loop.
    // No function call overhead. No flash fetch.
  }
};
```

`FORCE_INLINE` is defined as:
```cpp
#define FORCE_INLINE inline __attribute__((always_inline))
```

### IN_RAM and IN_FLASH Macros

```cpp
// Pico SDK form (preferred — names the section for debugging):
#define IN_RAM   __not_in_flash("centipede")
#define IN_FLASH __in_flash("FLASH")

// Generic form (works without Pico SDK):
#define IN_RAM   __attribute__((section(".time_critical")))
#define IN_FLASH
```

### Summary: What Goes Where

| Category | Attribute | Runs From | Example |
|----------|-----------|-----------|---------|
| Foreground inner loop | `IN_RAM` free function | RAM | `centipede_foreground_loop<T>()` |
| Hot-path helpers | `FORCE_INLINE static` method | Inlined into RAM caller | `T::ReadScsFloppy()` |
| Background loop | `IN_RAM` free function | RAM | `v4_background_loop<T>()` |
| One-time setup | `IN_FLASH` or unmarked | Flash (fine — called once) | `InitializePins()` |
| Boot/init code | unmarked | Flash | `main()`, `RunCores()` |

## 5. Dual-Core Architecture

```
┌─────────────────────────────────────────────┐
│  Core 1 (Foreground)        IN_RAM          │
│  ─────────────────────────────────────────  │
│  • Interrupts DISABLED                      │
│  • Runs bus cycle loop in lockstep with PIO │
│  • Reads/writes ram[], IOReaders/IOWriters  │
│  • Pushes events to fg2bg FIFO              │
│  • FlowControlCheck() every cycle           │
│  • MUST NEVER BLOCK OR STALL               │
└──────────────────┬──────────────────────────┘
                   │  CrossCoreFIFO<uint, 8192>
                   │  (lock-free, single-producer/single-consumer)
┌──────────────────▼──────────────────────────┐
│  Core 0 (Background)       IN_RAM           │
│  ─────────────────────────────────────────  │
│  • Interrupts ENABLED                       │
│  • Round-robin coroutine scheduler:         │
│    drain_task  → pops fg2bg, handles        │
│                  putchar/NMI/trace inline,  │
│                  dispatches floppy/spoon    │
│    floppy_task → sector read/write via VFS  │
│    spoon_task  → Tcl console (gspoon)       │
│  • PumpUsbCobs() between every task switch  │
└─────────────────────────────────────────────┘
```

### Flow Control

The fg2bg FIFO has watermark-based HALT throttling:

```cpp
FORCE_INLINE void IN_RAM FlowControlCheck() {
  if (fg_halt_for_flow_control) {
    if (fg2bg.size() < FG2BG_LOW_WATERMARK)  { HaltOff(); fg_halt_for_flow_control = false; }
  } else {
    if (fg2bg.size() > FG2BG_HIGH_WATERMARK) { HaltOn();  fg_halt_for_flow_control = true; }
  }
}
```

When the FIFO fills up, the foreground asserts HALT on the 6809, pausing
the CPU until the background drains enough entries. This prevents overflow
without blocking the foreground (HALT is checked by the CPU on the next
bus cycle, not by the Pico).

## 6. Open-Drain GPIO for Bus Signals

All active-low control signals to the 6809 (RESET, IRQ, FIRQ, NMI, HALT)
use **open-drain** GPIO, not push-pull:

```cpp
// Initialization: output latch = 0, direction = input (released)
gpio_set_dir(pin, GPIO_OUT);
gpio_put(pin, 0);             // Latch = 0
gpio_set_dir(pin, GPIO_IN);   // Released (pulled high by pull-up)
gpio_set_pulls(pin, true, false);

// Assert (pull low): set direction to output
gpio_set_dir(pin, GPIO_OUT);  // Pin driven low via latch=0

// Release (float high): set direction to input
gpio_set_dir(pin, GPIO_IN);   // Pin floats high via pull-up
```

**Why open-drain?** These are active-low signals that may be driven by
multiple devices on the bus (wired-OR). Push-pull would fight with other
drivers. Open-drain allows multiple devices to assert simultaneously.

## 7. IOReaders / IOWriters Arrays

Device I/O at `$FF00-$FFFF` is dispatched through function pointer arrays:

```cpp
using IOReader = byte (*)(uint addr);    // Raw function pointer (not std::function!)
using IOWriter = void (*)(uint addr, byte data);

IOReader IOReaders[256];  // Indexed by low byte of address
IOWriter IOWriters[256];
```

**Critical: these must be raw function pointers, not `std::function`.**
`std::function` uses type erasure with heap allocation — disastrous in a
250 MHz foreground loop. Raw pointers are a single indirect call.

A `nullptr` entry means "no device at this address" — the foreground falls
through to bus passthrough (Centipede) or returns `ram[addr]` (TFR911).

## 8. NMI Edge-Trigger Handling

The 6809's NMI is **edge-triggered** (not level). The pin must return high
before the next NMI can be recognized. This creates a timing constraint:

```
Foreground (core 1):           Background (core 0):
  ASSERT_NMI()                   drain_task top-of-loop:
  nmi_pending = true               if (nmi_pending) {
  PUSH_TO_BG(FG2BG_NMI,...)          nmi_pending = false;
                                      RELEASE_NMI();
                                   }
```

**Why not use the FIFO for NMI release?** The fg2bg FIFO may have thousands
of trace entries queued. By the time a `FG2BG_NMI` entry is popped, the
6809 may have already missed the NMI edge. The volatile `nmi_pending` flag
is checked at the **top** of every drain_task iteration with highest priority.

## 9. The "Include, Don't Rewrite" Strategy

The v1 firmware is ~6000 lines across ~30 header files with deep
interdependencies (coroutines, VFS, Tcl, COBS, USB pipeline). Rather than
rewriting all of it for v4, the v4 main files `#include` the v1 headers
directly:

```cpp
// v4_centipede_main.cpp
#include "../v1/firmware/console.h"
#include "../v1/firmware/floppy.h"
#include "../v1/firmware/gspoon.h"
#include "../v1/firmware/coco64k.h"
// ... etc
```

The v1 headers already use the same CRTP `Do*/Dont*` pattern. A compatibility
shim (`v4_compat_centipede.h`) bridges any naming differences.

### Include Ordering Is Critical

The v1 headers are designed to be `#include`d from a single `.cpp` file in a
specific order. They depend on globals and macros defined earlier in the
translation unit. The v4 main must replicate this exact order:

```
console.h → keyboard_injector.h → coro.h → flash-label.h → config.h →
floppy.h → rtc.h → [spoon_has_work declaration] → gspoon.h → tcl_io.h →
vfs.h → coco64k.h → littlefs.h → tcl_commands.h → pcb.h → pico_rpc.h
```

Getting this wrong causes cascading "not declared in this scope" errors.

## 10. Surprising Discoveries

### 1. `__attribute__((section))` silently fails on class methods
GCC accepts the syntax without warning but does **not** place the method in
the specified section. This was the root cause of flash stalls that corrupted
bus data. The fix: free templated functions with `FORCE_INLINE static` helpers.

### 2. `std::function` has massive overhead
Each `std::function` call involves type erasure, potential heap allocation,
and an indirect call through a vtable-like mechanism. Replacing
`std::function<byte(uint)>` with `byte (*)(uint)` eliminated measurable
overhead in the foreground loop where `IOReaders[addr]` is called every cycle.

### 3. `constexpr` vs `volatile` for Speed enum
The v1 code uses `constexpr TracingSpeed Speed = MEDIUM_SPEED;` — this allows
the compiler to optimize out entire code paths at compile time (e.g.,
`if (Speed <= SLOW_SPEED)` becomes dead code). Using `volatile` would defeat
this optimization and add a memory read every bus cycle.

### 4. The 6809's `$FF40` (latch) is written AFTER `$FF48` (command)
Disk BASIC writes the FDC command register before the drive-select latch.
This means at command time, `floppy_latch` still has the OLD value. The
correct latch must be snapshotted later (at byte 256 during writes). Getting
this timing wrong causes writes to go to the wrong drive.

### 5. NMI release via FIFO is too slow
The fg2bg FIFO can hold 8192 entries of trace data. At full tracing speed,
it takes hundreds of milliseconds for a `FG2BG_NMI` entry to be popped.
The 6809 needs NMI released within microseconds. Solution: volatile flag
checked at top of drain loop, not through the FIFO.

### 6. Flow control HALT must be foreground-only
Early versions had the background calling `HaltOff()` to release the CPU
after draining some FIFO entries. This defeated the watermark-based
throttling, causing FIFO overflow. HALT for flow control must be managed
exclusively by `FlowControlCheck()` on the foreground core.

### 7. Cross-core config structs need `volatile` fields
`CentipedeConfig` is modified by the menu (background core 0) and read by
the foreground loop (core 1). Without `volatile`, GCC `-O2` propagates the
initial values from `main()` as compile-time constants into the
`FORCE_INLINE` foreground loop, ignoring runtime changes from the other core.
Symptom: `trace_reads=true` set by the menu has no effect — only I/O-page
reads appear (from the `trace_writes` fallback condition). Fix: make all
`CentipedeConfig` fields `volatile bool`.

### 8. Protocol constants C_RAM2_READ ≠ C_RAM2_WRITE
`C_RAM2_WRITE = 195` (0xC3), `C_RAM2_READ = 211` (0xD3). These are different
command bytes — confusing them causes read packets to be decoded as writes
by the tether, making read tracing silently fail.

## 11. Build System

```bash
# From v3/merging/centipede/v4/:
mkdir -p build && cd build
PICO_SDK_PATH=/path/to/pico-sdk cmake ..
make -j$(nproc)
ls -l centipede_v4.uf2   # ~1.1 MB, ready to flash
```

Requires: Pico SDK 2.x, `arm-none-eabi-gcc` 13+, cmake 3.12+.
Target board: `solderparty_rp2350_stamp_xl` (RP2350B, 48 GPIO).
