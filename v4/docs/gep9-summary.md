# GEP9 Engine & Hatvan OS on TFR/911h

The **`gep9` execution engine** enables the **TFR/911h Single Board Computer** (Hitachi 6309E CPU + Raspberry Pi RP2350B co-processor) to boot and run the **Hatvan operating system**, matching the execution environment of the software emulator launcher (`~/github.com/strickyak/hatvan-os/run9.sh`).

---

## 1. User Guide: How to Run Hatvan OS

### Prerequisites & Staging
The Hatvan OS build artifacts must be accessible in the Tether Virtual File System mounted under `/pc`. The repository comes pre-configured with symlinks in [`misc/pc/`](file:///home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/):
- `kernel_6809.decb` -> Hatvan monolithic kernel binary (Task 0)
- `rbf_6809.decb` -> Hatvan RBF filesystem driver (Task 1)
- `procfs_6809.decb` -> Hatvan PROCFS driver (Task 2)
- `hatvan0.dsk` (or `disk0.dsk`) -> 10 MB OS-9 RBF root filesystem

To stage these manually into `/tmp/pc` (if using custom builds):
```bash
mkdir -p /tmp/pc
cp -u /home/strick/github.com/strickyak/hatvan-os/build/kernel_6809.decb /tmp/pc/
cp -u /home/strick/github.com/strickyak/hatvan-os/build/rbf_6809.decb    /tmp/pc/
cp -u /home/strick/github.com/strickyak/hatvan-os/build/procfs_6809.decb /tmp/pc/
cp -u /home/strick/github.com/strickyak/hatvan-os/build/disk0.dsk        /tmp/pc/hatvan0.dsk
cp -u /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/boot_gep9.tcl      /tmp/pc/
cp -u /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/mode80.tcl         /tmp/pc/
```

---

### Launch Methods

#### Method 1: Dedicated Launcher Script (Recommended)
Run [`run_gep9.sh`](file:///home/strick/modoc/coco-shelf/tfr9/v4/run_gep9.sh) from the repository root:
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4
./run_gep9.sh
```
This automatically invokes Tether mounting `misc/pc` as `/pc` with `-bootmode=80`.

#### Method 2: Boot Mode 80 via Tether
Run Tether directly with `-bootmode=80`:
```bash
sh tether.sh -bootmode=80
```
This executes [`misc/pc/mode80.tcl`](file:///home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/mode80.tcl), which sources [`misc/pc/boot_gep9.tcl`](file:///home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/boot_gep9.tcl):
```tcl
set Config(engine) "gep9"
set Config(flat_img) "/pc/kernel_6809.decb"
set Config(task1) "/pc/rbf_6809.decb"
set Config(task2) "/pc/procfs_6809.decb"
set Config(drive0) "/pc/hatvan0.dsk"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye
```

#### Method 3: Interactive Tcl Session
1. Launch Tether into the Phase 1 Tcl REPL:
   ```bash
   sh tether.sh
   ```
2. Configure the `gep9` engine parameters and exit Tcl:
   ```tcl
   TCL> set Config(engine) "gep9"
   TCL> set Config(flat_img) "/pc/kernel_6809.decb"
   TCL> set Config(task1) "/pc/rbf_6809.decb"
   TCL> set Config(task2) "/pc/procfs_6809.decb"
   TCL> set Config(drive0) "/pc/hatvan0.dsk"
   TCL> menu store Config
   TCL> bye
   ```

---

### Live Execution Transcript

Below is a complete capture of Hatvan OS booting on the physical Hitachi 6309E CPU, mounting the root filesystem, reaching the interactive shell (`$ `), executing interactive commands, and exiting cleanly:

```text
[BYE — starting 6309]
[gep9: opened drive 0: /pc/hatvan0.dsk]
[gep9: loading /pc/kernel_6809.decb into Task 0...]
[gep9: parsing Extended DECB format...]
[gep9: DECB execution address $1000 set in reset vector]
[gep9: Task 0 image load complete]
[gep9: loading /pc/rbf_6809.decb into Task 1...]
[gep9: loading /pc/procfs_6809.decb into Task 2...]
[gep9: IO and MMU initialized | preloaded 164 / 1024 pages (Task 0: 105, Task 1: 47, Task 2: 12)]
[bye: launching core1...]
[bye: HALT released, 6309 booting]
    --  Hatvan OS
    --  cstart: 1000
    --  main: 1373
    --  TaskFuse armed with task 1
    --  Returned from f_hal__InitRBF
    --  TaskFuse armed with task 2
    --  Returned from f_hal__InitProcFS
    --  Returned from f_hal__MountRootRBF
    --  InitRoot: 0
    --  main end: 0
    --  SHELL:FORK:OK

[gep9 shell running: 216 / 1024 physical pages allocated (54.0 KB / 256 KB)]
  Task  0: 141 pages (36096 bytes / 35.2 KB)
  Task  1:  56 pages (14336 bytes / 14.0 KB)
  Task  2:  15 pages ( 3840 bytes /  3.8 KB)
  Task  3:   4 pages ( 1024 bytes /  1.0 KB)
$ help
Commands: help gecho gcat gdir exit
$ gecho Hello from Hatvan OS on Hitachi 6309
Hello from Hatvan OS on Hitachi 6309
$ gdir
Cmds
test_expr.sh
test_while.sh
triangle.sh
$ exit
 *** GEP9 Exiting with status 0
[gep9 on exit: 212 / 1024 physical pages allocated (53.0 KB / 256 KB)]
  Task  0: 141 pages (36096 bytes / 35.2 KB)
  Task  1:  56 pages (14336 bytes / 14.0 KB)
  Task  2:  15 pages ( 3840 bytes /  3.8 KB)

[tether: target shutdown OK (0)]
Received C_SHUTDOWN status 0; exiting cleanly
```

---

## 2. Physical Page Utilization (256 KB / 1,024 Pages)

During the boot lifecycle to an interactive shell prompt:

| Lifecycle Phase | Total Pages Allocated | Memory Used | Memory Free (Pool) | Per-Task Allocation Breakdown |
| :--- | :--- | :--- | :--- | :--- |
| **1. Firmware Preload** | **164 / 1,024** | **41.0 KB** (16.0%) | **860 pages** (215.0 KB / 84.0%) | • **Task 0 (Kernel)**: 105 pages (26.9 KB)<br>• **Task 1 (RBF Driver)**: 47 pages (12.0 KB)<br>• **Task 2 (ProcFS Driver)**: 12 pages (3.1 KB) |
| **2. Interactive Shell (`$ `)** | **216 / 1,024** | **54.0 KB** (21.1%) | **808 pages** (202.0 KB / **78.9%**) | • **Task 0 (Kernel)**: 141 pages (+36 pages for dynamic heap, mount tables, stack)<br>• **Task 1 (RBF Driver)**: 56 pages (+9 pages for sector buffers and state)<br>• **Task 2 (ProcFS Driver)**: 15 pages (+3 pages for proc tables)<br>• **Task 3 (Shell `gsh`)**: 4 pages (1.0 KB entry point and stack) |
| **3. After Shell Exit (`exit`)** | **212 / 1,024** | **53.0 KB** (20.7%) | **812 pages** (203.0 KB / 79.3%) | • Task 3 terminates; `PurgeTaskMem` (`$FF2F`) immediately zeroes and returns all 4 pages to the free pool. |

---

## 3. Implementation Summary

### 1. Sparse Demand-Paged MMU ([`tfr911h/v4_gep9_mmu.h`](file:///home/strick/modoc/coco-shelf/tfr9/v4/tfr911h/v4_gep9_mmu.h))
- **Task & Address Space Architecture**:
  - Supports **256 distinct tasks**, each possessing an independent 64 KB address space organized as 256 pages of 256 bytes.
  - Dynamically backed by a 256 KB physical SRAM pool on the RP2350B, partitioned into **1,024 page frames**.
- **Page Allocation & Freeing**:
  - Page frame 0 is permanently zeroed and mapped to all unallocated pages for reads.
  - Writes to unallocated pages trigger dynamic allocation from the free pool on demand.
  - `PurgeTaskMem` (`$FF2F`): When a task terminates, writing its task ID zeroes and releases all allocated page frames back to the available pool.
- **Shared Memory Curtain (`$FF28..$FF29`)**:
  - Configurable shared curtain (default `$E000`). For kernel and driver tasks (Tasks 0..2), memory accesses above the curtain resolve to Task 0's address space to facilitate low-overhead interrupt vector dispatch and inter-task I/O buffering.
- **Hardware `TaskFuse` (`$FF20`)**:
  - Writing a target task ID $T$ to `$FF20` arms the hardware fuse (`task_fuse_armed = 1`).
  - Upon fetching the `RTI` ($3B) instruction, Core 1 atomically switches `current_task = fuse_target` and disarms the fuse.
  - Hardware interrupts and software traps (`SWI2`) save state to the active user stack and vector back to Task 0.

### 2. Hatvan Peripheral I/O Emulation ([`tfr911h/v4_gep9_io.h`](file:///home/strick/modoc/coco-shelf/tfr9/v4/tfr911h/v4_gep9_io.h))
- **Console UART (`$FF00..$FF05`)**:
  - `$FF00`: `Term.Out` (transmit character to host console).
  - `$FF01`: `Term.In` (receive character from host input buffer).
  - `$FF02`: `Reg.Stat` (status flags: Bit 0 = `Timer.Ready`, Bit 1 = `Term.RxReady`, Bit 2 = `Term.TxReady`).
  - `$FF03`: `Reg.Ctrl` (interrupt enable control: Bit 0 = `TimrIRQ`, Bit 1 = `TermIRQ`).
  - `$FF04`: `logchar` (kernel diagnostic character logging).
  - `$FF05`: `Exit.Code` (triggers clean target shutdown and returns exit status to Tether).
- **Virtual Disk Controller (`$FF10..$FF17`)**:
  - Supports up to 4 virtual drives (`/d0` through `/d3`) backed by VFS / LittleFS disk images.
  - Direct sector DMA transfers 256-byte blocks directly between host file handles and `[Disk.Task : Disk.Addr]`.
- **Fast DMA Block Copy (`$FF21..$FF27`)**:
  - Hardware-accelerated memory-to-memory block transfers across task boundaries.
- **Dynamic IRQ Line Driving**:
  - Automatically manages the 6309 `/IRQ` pin based on unmasked pending status bits (`Timer.Ready` and `Term.RxReady`).

### 3. Multi-Task Preloading & Firmware Integration
- **Configuration Extension**:
  - Extended [`CentipedeConfig`](file:///home/strick/modoc/coco-shelf/tfr9/v4/firmware/one/firmware/config.h) and [`menu.h`](file:///home/strick/modoc/coco-shelf/tfr9/v4/firmware/one/firmware/menu.h) with fields `task1` and `task2`.
- **DECB Multi-Task Preloader**:
  - Extended DECB parser in [`tfr911h/v4_tfr911h_main.cpp`](file:///home/strick/modoc/coco-shelf/tfr9/v4/tfr911h/v4_tfr911h_main.cpp) (`gep9_load_decb_into_task()`) loads:
    - `Config(flat_img)` into **Task 0** (latches execution entry point `$1000` into reset vector).
    - `Config(task1)` into **Task 1** (Hatvan RBF driver).
    - `Config(task2)` into **Task 2** (Hatvan PROCFS driver).
    - Mounts `Config(drive0)` on drive 0.

### 4. Critical Hardware Timing & Vector Trap Resolution
- **Hitachi 6309 / 6809 Dead-Cycle Phenomenon**:
  - During internal dead cycles (e.g. index register calculations, stack alignment, multi-cycle ALU operations), Motorola 6809 and Hitachi 6309 CPUs drive `$FFFF` on the address bus with read status (`R/W=1`).
- **The Vector Trap Bug**:
  - An initial vector-handling check `if (addr >= 0xFFF0) gep9_mmu.current_task = 0;` triggered on every internal dead cycle (`$FFFF >= $FFF0`).
  - This caused the MMU to slam `current_task` back to Task 0 in the middle of executing Task 1 instructions, corrupting return stacks and causing an infinite boot loop.
- **The Fix**:
  - Vector fetch task transition was updated to strictly require:
    ```cpp
    if (addr >= 0xFFF0 && addr != 0xFFFF && (addr & 1) == 0) {
        // Valid start of 16-bit interrupt/trap vector fetch
        gep9_mmu.current_task = 0;
    }
    ```
  - Ignoring `$FFFF` ensures vector transitions occur strictly on legitimate vector fetch cycles.
