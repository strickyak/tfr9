# Architecture Proposal: "Middleground" Execution Tier (Rejected)

* **Date**: 2026-09-27
* **Status**: **Rejected**
* **Branch**: `work-sept25-try-coco3`
* **Topic**: Inter-Core Precomputation Tier for 6809 Bus Emulation on Centipede RP2350B

---

## 1. Context & The "Middleground" Concept

In Centipede's dual-core architecture on the RP2350B, processing is currently divided into two tiers:

1. **Foreground (Core 1, Bus Cycle Loop)**:
   * Runs in lockstep with the Gerbil PIO state machine at the host 6809 CPU clock (~0.895 MHz, ~1,118 ns per cycle).
   * Has strict real-time deadlines: Phase 4 read data must be driven on `dbus` within **~250 ns** of $E$ rising.
   * Must execute **100% from SRAM** (`__not_in_flash`), with zero blocking and zero flash memory access.

2. **Background (Core 0, Coroutines & Polling)**:
   * Processes the inter-core `fg2bg` software FIFO.
   * Handles non-time-critical services: USB CDC serial communication with Tether, LittleFS file system calls, floppy disk sector caching/transmission, and Tcl scripting.
   * Executes largely from QSPI Flash (`.text` in `0x1000xxxx`).

### The Proposed Tier: "Middleground"

The proposal introduced a middle tier for operations too heavy to perform inside the tight ~250 ns Phase 4 foreground window, but required too quickly to wait for the asynchronous `fg2bg` FIFO queue (e.g., multi-entry MMU table calculations or composite memory map updates):

* **Mechanism**:
  1. When a bus event occurs that alters memory mapping (such as a write to GIME MMU registers `$FFA0`–`$FFAF` or SAM mode bits `$FFC0`–`$FFDF`), Core 1 (Foreground) writes the trigger parameters (address and data) into a shared scratchpad struct in SRAM.
  2. Core 1 signals Core 0 via a dedicated hardware interrupt (e.g., RP2350 SIO Doorbell).
  3. Core 0 immediately takes the interrupt, halts its background tasks, runs a high-priority **Middleground ISR** in SRAM to compute and update the precomputed lookup tables (e.g., `mmu_base[]`), and returns from interrupt back to its background work.
  4. Core 1 does not wait or block; it expects the updated table to be ready for subsequent bus cycles.

---

## 2. Hardware Primitives on RP2350

The RP2350 provides strong hardware support for inter-core signaling:
* **SIO Doorbells (`SIO_DOORBELL0`–`DOORBELL3`)**: Core 1 can trigger an immediate NVIC interrupt on Core 0 with a single 1-cycle register write (`sio_hw->doorbell_out_set = 1`).
* **Low NVIC Latency**: On the Cortex-M33 at 250 MHz, hardware interrupt entry takes 12–16 cycles (~50–64 ns).
* **Shared SRAM Crossbar**: Both cores access SRAM through the system crossbar with no L1 cache coherency overhead; writes to SRAM are visible across cores as soon as write buffers drain via a Data Synchronization Barrier (`dsb`).

---

## 3. The Two Deal-Breakers for Rejection

Despite the elegant conceptual model, detailed timing and codebase analysis identified two critical, disqualifying deal-breakers:

### Deal-Breaker 1: Background Interrupt Disable Latency

* **The Next-Cycle Dependency**:
  On the 6809, an instruction that writes to an MMU register (e.g., `STA $FFA0` or `STB $FF90`) completes on bus cycle $N$.
  Only **1,118 ns later** (cycle $N+1$), the 6809 fetches the opcode of the next instruction from its Program Counter ($PC$).
  If the write remapped the memory bank where code is actively executing (or performed a task switch), cycle $N+1$ **immediately requires the updated translation**.
* **Interrupt Lockout in Background Code**:
  Standard Raspberry Pi Pico SDK calls, TinyUSB CDC driver interrupt routines, spinlocks, and LittleFS SPI Flash operations routinely call:
  ```c
  save_and_disable_interrupts();
  ```
  If Core 0 is inside a critical section when Core 1 writes to `$FFA0`, the Middleground ISR will be deferred until interrupts are re-enabled. A critical section lasting just **2 to 5 µs** is completely normal in USB/filesystem code, but causes Core 1 to service cycle $N+1$ using stale MMU tables, corrupting instruction execution and crashing the 6809.

### Deal-Breaker 2: Background Codebase Cannot Guarantee 100% SRAM

* For Middleground to achieve predictable sub-microsecond latency, the ISR function, its vector table entry, and every helper function or constant table it accesses must be guaranteed to reside in SRAM (`__not_in_flash`).
* In a substantial C++ codebase integrating LittleFS, MiniZ, Tcl, and the Pico SDK, enforcing and guaranteeing that background helper routines never branch out of SRAM is extremely brittle. If an ISR or helper function ever calls an out-of-line method or encounters a QSPI cache miss while Core 0 is servicing flash, Core 0 will stall, missing the tight bus cycle window.

---

## 4. Comparison: Middleground vs. Inlined Foreground

Evaluating the actual cost of MMU register updates confirmed that Middleground is not necessary:

* **Direct Foreground Execution Cost**:
  When the 6809 writes byte $D$ to `$FFA0` (remapping an 8KB bank), the translation update is:
  ```cpp
  mmu_base[reg & 0xF] = (dbus & 0x3F) << 13;
  ```
  In ARM assembly, this is compiled to just **3 instructions (12 nanoseconds)** on Core 1:
  ```asm
  ubfx r1, r2, #0, #6        ; 1 cycle (extract 6 bits)
  lsl  r1, r1, #13           ; 1 cycle
  str  r1, [r0, r3, lsl #2]  ; 1 cycle
  ```
* **Performance Comparison**:
  * Inlined Foreground update: **12 ns**, deterministic, immune to interrupt disable latency.
  * Middleground inter-core ISR: **~250–400 ns** best-case, unbounded worst-case (subject to interrupt disable latency).

Direct synchronous execution on Core 1 is **20× faster** than an inter-core interrupt round-trip and carries zero synchronization risk.

---

## 5. Architectural Decision

1. **The "Middleground" tier is rejected.**
2. Real-time bus updates (MMU base tables, I/O registers, SAM state bits) will remain strictly **inlined directly within Core 1 Foreground** using `FORCE_INLINE` and `IN_RAM`.
3. Non-time-critical services will continue to use the existing lock-free `fg2bg` FIFO queue to Core 0 Background.
