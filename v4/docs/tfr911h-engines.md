# TFR/911h Firmware Engines & Operating Systems

The **TFR/911h** single-board computer pairs a physical **Hitachi 6309E microprocessor** with a **Raspberry Pi RP2350B co-processor**. The RP2350 co-processor generates the quadrature bus clocks ($E$ and $Q$) via its PIO state machine (Hamster), emulates peripheral I/O devices, manages physical memory mapping and DMA block transfers, and bridges serial communication to the host workstation via USB CDC-ACM.

The unified TFR/911h firmware ([`v4/tfr911h/v4_tfr911h_main.cpp`](file:///home/strick/modoc/coco-shelf/tfr9/v4/tfr911h/v4_tfr911h_main.cpp)) supports **four distinct CPU execution engines**:

1. **Turbo9Sim / Turbo9OS** (`ENGINE_TURBO9OS`, `0`): Legacy embedded OS-9 Level 1 ROM bundle with simulated ACIA.
2. **Flat65280** (`ENGINE_FLAT65280`, `1`): Clean flat 64 KB memory model for NitrOS-9 Level 1 direct boot without banking hardware.
3. **Deep65280** (`ENGINE_DEEP65280`, `2`): Multi-tasking 256 KB memory model for NitrOS-9 Level 2 using a GIME/CoCo 3-style paged MMU.
4. **gep9 (Hatvan 6309)** (`ENGINE_GEP9`, `3`): Demand-paged Hatvan operating system engine with 256 tasks, hardware `TaskFuse` context switching, and Fast DMA block transfers.

---

## 1. Architectural Comparison of the Four Engines

| Feature | **Turbo9Sim (Turbo9OS)** | **Flat65280** | **Deep65280** | **gep9 (Hatvan 6309)** |
| :--- | :--- | :--- | :--- | :--- |
| **Operating System** | Turbo9OS / OS-9 Level 1 (custom ROM bundle with Basic09, Ax, Ncl) | NitrOS-9 Level 1 | NitrOS-9 Level 2 | **Hatvan OS** (OS-9 system call compatible, Unix-like byte I/O, synchronous fork/exec/chain) |
| **Physical RAM** | 64 KB SRAM (`ram[0x0000..0xFFFF]`) | 64 KB SRAM (`ram[0x0000..0xFFFF]`) | **256 KB** SRAM (32 physical blocks of 8 KB each) | **256 KB** SRAM dynamically allocated as **1,024 page frames** of 256 bytes each |
| **Memory Architecture** | **Flat 64 KB split RAM/ROM**: Low memory is system RAM; pre-linked OS ROM modules are copied into high memory ending just below `$FF00`. | **Flat 64 KB**: `$0000..$FEFF` (65,280 bytes) is continuous flat RAM. `$FF00..$FFFF` (256 bytes) is hardware I/O and CPU vectors. Zero MMU required. | **Paged MMU (CoCo 3 / GIME style)**: Eight 8 KB logical address slots (`$0000..$E000`) dynamically mapped to 8 KB physical blocks in 256 KB RAM. | **Sparse Demand Paging**: 256 isolated tasks, each with 256 256-byte pages (64 KB). Frame 0 permanently zeroed. Shared curtain ($FF28, default `$E000`) for Tasks 0..2. |
| **MMU & Tasks** | None. Single address space. | None. Single address space. | **Dual Task MMU** (`$FFA0..$FFAF`): Task 0 and Task 1. Active task at `$FF91`. Trampoline page `$FE00..$FEFF` in block 31. | **Two-Level Dynamic MMU**: Hardware `TaskFuse` (`$FF20`) arms user context switch on `RTI`. `PurgeTaskMem` (`$FF2F`) frees task pages. Fast DMA (`$FF21..$FF27`). TaskFlags (`$FF2D..$FF2E`). |
| **Console UART** | Proprietary **Turbo9Sim ACIA** at `$FF00..$FF03`: Data TX (`+0`), RX (`+1`), Status (`+2`), Control (`+3`). | Standard Motorola **ACIA 6850** at `$FF88..$FF89`: Control/Status (`$FF88`), Data (`$FF89`). Driver: `sc6850`. | Standard Motorola **ACIA 6850** at `$FF88..$FF89`. Driver: `sc6850`. | **Hatvan Console**: Non-blocking `Term.In` (`$FF01`), `Term.Out` (`$FF00`), `Reg.Stat` (`$FF02`), `Reg.Ctrl` (`$FF03`), `logchar` (`$FF04`), `Exit.Code` (`$FF05`). |
| **Virtual Disks** | None (all modules reside in in-memory ROM/RAM). | **EMUDSK controller** at `$FF80..$FF86`: Synchronous 256-byte sector DMA backed by host or flash files (`drive0`, `drive1`). | **EMUDSK controller** at `$FF80..$FF86`: DMA buffer addresses translated across task MMU tables. | **Hatvan Disk Controller** at `$FF10..$FF17`: Direct DMA between `/d0`..`/d3` and `[Disk.Task : Disk.Addr]`. Supports up to 4 virtual drives. |
| **Timer / Interrupts** | 60 Hz timer driving simulated ACIA status/control bits. | **CLOCK_AND_STOP** at `$FF87`: Configurable periodic ticks (60Hz, 50Hz, 6Hz, 0.1Hz) and stop (`$FC`). | **CLOCK_AND_STOP** at `$FF87` + GIME Init0 register at `$FF90`. | **Turbo9Sim 60 Hz Timer**: Bit 0 of `$FF02` (`Timer.Ready`), enabled via Bit 0 of `$FF03` (`TimrIRQ`). |
| **Boot Image Source** | Compiled into firmware as C++ byte arrays (`turbo9os.rom.h`, `basic09.mod.h`). | Loaded from VFS file via `Config(flat_img)` (e.g. `/pc/flat65280.img`). Has embedded flash fallback. | Loaded from VFS file via `Config(flat_img)` (e.g. `/pc/deep65280.img`). | Loaded from VFS file via `Config(flat_img)` (e.g. `/pc/kernel_6809.decb`): Extended DECB chunks parsed directly into Task 0; execution address latched into reset vector. |

---

## 2. Where Recipe Images & Disks are Built

The NitrOS-9 OS boot images (`.img`) and disk files (`.dsk`) are maintained in the [`nitros9`](file:///home/strick/modoc/coco-shelf/nitros9/) project tree under `recipes/`:

### Flat65280 Recipe (NitrOS-9 Level 1)
- **Directory**: [`../../nitros9/recipes/flat65280/`](file:///home/strick/modoc/coco-shelf/nitros9/recipes/flat65280/)
- **Primary Targets**:
  - `flat65280.img`: 65,536-byte NitrOS-9 Level 1 monolithic boot image containing the kernel (`krn`, `krnp2`), I/O system (`ioman`, `rbf`, `scf`), drivers (`emudsk`, `sc6850`, `clock`), and core utilities.
  - `flat0.dsk`: 10 MB virtual disk image mounted on EMUDSK Drive 0 containing commands (`/dd/CMDS`), source files, and libraries.
- **Build Command**:
  ```bash
  cd /home/strick/modoc/coco-shelf/nitros9/recipes/flat65280
  make
  ```

### Deep65280 Recipe (NitrOS-9 Level 2)
- **Directory**: [`../../nitros9/recipes/deep65280/`](file:///home/strick/modoc/coco-shelf/nitros9/recipes/deep65280/)
- **Primary Targets**:
  - `deep65280.img`: 65,536-byte NitrOS-9 Level 2 boot image structured for MMU paging: Slot 0 (8 KB, block 0) holds low-memory tables and vector handlers; Slots 1..7 (56 KB, blocks 25..31 / `$39..$3F`) hold the Level 2 kernel, IOMan, RBF, and system descriptors.
  - `disk0.dsk` (often symlinked as `deep0.dsk`): 10 MB virtual disk image for EMUDSK Drive 0.
- **Build Command**:
  ```bash
  cd /home/strick/modoc/coco-shelf/nitros9/recipes/deep65280
  make
  ```

### gep9 Recipe (Hatvan 6309 OS)
- **Directory**: `/home/strick/github.com/strickyak/hatvan-os/`
- **Primary Targets**:
  - `build/kernel_6809.decb`: Hatvan 6309/6809 monolithic kernel binary in Extended DECB format, including drivers, vectors, and reset entry point `cstart` at `$1000`.
  - `build/disk0.dsk`: 10 MB OS-9 RBF filesystem image mounted on `/d0` containing user shell `gsh` and utilities (`gcat`, `gdir`, `gecho`).
- **Build Command**:
  ```bash
  cd /home/strick/github.com/strickyak/hatvan-os
  make
  ```

---

## 3. How to Choose an Engine

The RP2350 firmware executes in two distinct lifecycle phases:
- **Phase 1 (Pre-Boot & Configuration)**: Runs an interactive **Tcl 6.7 REPL** on Core 0. The physical 6309E CPU is held in hardware `RESET` and `HALT`.
- **Phase 2 (Active 6309 Execution)**: Triggered when Tcl exits via `bye`. Core 0 configures hardware memory and I/O handlers for the selected engine, launches Core 1 to run the real-time PIO clock and bus loop, and releases `RESET` and `HALT`.

### Configuration Parameters via `menu`
Engine configuration is managed using standard Tcl array variables stored with the `menu store Config` command:

| Variable | Type | Allowed Values | Purpose |
| :--- | :--- | :--- | :--- |
| `Config(engine)` | string | `"turbo9os"` (or `"turbo9sim"`),<br>`"flat65280"` (or `"flat"`),<br>`"deep65280"` (or `"deep"`, `"deep64280"`),<br>`"gep9"` (or `"hatvan"`) | Selects which engine initializes when `bye` is invoked. Default is `"turbo9os"`. |
| `Config(flat_img)` | string | VFS path (e.g. `"/pc/flat65280.img"`, `"/pc/deep65280.img"`, `"/pc/kernel_6809.decb"`) | Path to OS boot image or DECB binary loaded into physical RAM (Task 0 for `gep9`). |
| `Config(task1)` | string | VFS path (e.g. `"/pc/rbf_6809.decb"` or `""`) | DECB binary preloaded into Task 1 for `gep9` (Hatvan RBF driver). |
| `Config(task2)` | string | VFS path (e.g. `"/pc/procfs_6809.decb"` or `""`) | DECB binary preloaded into Task 2 for `gep9` (Hatvan PROCFS driver). |
| `Config(drive0)` | string | VFS path (e.g. `"/pc/disk0.dsk"`, `"/pc/flat0.dsk"`, `"/pc/hatvan0.dsk"`, or `""`) | Disk image attached to Drive 0 (EMUDSK or Hatvan `/d0`). |
| `Config(drive1)` | string | VFS path or `""` | Disk image attached to Drive 1 (EMUDSK or Hatvan `/d1`). |
| `Config(drive2)` | string | VFS path or `""` | Disk image attached to Hatvan Drive 2 (`/d2`). |
| `Config(drive3)` | string | VFS path or `""` | Disk image attached to Hatvan Drive 3 (`/d3`). |

---

## 4. Connecting with `tether.sh`

The host tether script [`tether.sh`](file:///home/strick/modoc/coco-shelf/tfr9/v4/tether.sh) mounts a host directory (default `/tmp/pc`) as `/pc` in the board's Virtual File System (VFS), redirects verbose USB RPC logging to `_log`, and connects standard input/output to the terminal:

```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4
sh tether.sh [flags...]
```

### Staging Recipe Files in `/tmp/pc`
Before launching engines that read from `/pc`, ensure the build artifacts are copied or symlinked into `/tmp/pc`:

```bash
mkdir -p /tmp/pc

# Stage Flat65280 files:
cp -u /home/strick/modoc/coco-shelf/nitros9/recipes/flat65280/flat65280.img /tmp/pc/
cp -u /home/strick/modoc/coco-shelf/nitros9/recipes/flat65280/flat0.dsk     /tmp/pc/

# Stage Deep65280 files:
cp -u /home/strick/modoc/coco-shelf/nitros9/recipes/deep65280/deep65280.img /tmp/pc/
cp -u /home/strick/modoc/coco-shelf/nitros9/recipes/deep65280/disk0.dsk     /tmp/pc/
ln -sf disk0.dsk /tmp/pc/deep0.dsk

# Stage gep9 (Hatvan) files:
cp -u /home/strick/github.com/strickyak/hatvan-os/build/kernel_6809.decb /tmp/pc/
cp -u /home/strick/github.com/strickyak/hatvan-os/build/disk0.dsk        /tmp/pc/
cp -u /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/boot_gep9.tcl      /tmp/pc/
cp -u /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/mode80.tcl         /tmp/pc/
```

---

## 5. Instructions to Launch Each Engine

### Engine 1: Turbo9Sim / Turbo9OS

Turbo9OS requires no external disk or boot image files since its ROM modules are built directly into the firmware.

#### Option A: Interactive Launch
1. Launch Tether into Phase 1:
   ```bash
   sh tether.sh
   ```
2. At the `TCL>` prompt, ensure the engine is `turbo9os` and exit:
   ```tcl
   TCL> set Config(engine) "turbo9os"
   TCL> menu store Config
   TCL> bye
   ```
3. Core 0 launches the 6309E into Turbo9OS. The terminal switches to the interactive Turbo9OS shell.

---

### Engine 2: Flat65280 (NitrOS-9 Level 1)

Flat65280 boots NitrOS-9 Level 1 using `flat65280.img` and mounts `flat0.dsk`.

#### Option A: Using a Boot Mode Script (`mode84.tcl`)
Create a boot mode script in `/tmp/pc/mode84.tcl` (or on the board's `/rc/`):
```tcl
set Config(engine) "flat65280"
set Config(flat_img) "/pc/flat65280.img"
set Config(drive0) "/pc/flat0.dsk"
set Config(drive1) ""
menu store Config
bye
```
Then launch directly with `-bootmode`:
```bash
sh tether.sh -bootmode=84
```

#### Option B: Interactive Launch via Tcl Shell
1. Launch Tether:
   ```bash
   sh tether.sh
   ```
2. In the `TCL>` prompt, configure the Flat65280 parameters and start:
   ```tcl
   TCL> set Config(engine) "flat65280"
   TCL> set Config(flat_img) "/pc/flat65280.img"
   TCL> set Config(drive0) "/pc/flat0.dsk"
   TCL> set Config(drive1) ""
   TCL> menu store Config
   TCL> bye
   ```
3. The board outputs:
   ```text
   [BYE — starting 6309]
   [flat65280: opened drive 0: /pc/flat0.dsk]
   [flat65280: loading /pc/flat65280.img into RAM...]
   [flat65280: loaded 65536 bytes into RAM]
   [bye: launching core1...]
   [bye: HALT released, 6309 booting]
   NitrOS-9 Level 1
   OS9:
   ```

---

### Engine 3: Deep65280 (NitrOS-9 Level 2)

Deep65280 boots NitrOS-9 Level 2 with MMU memory translation into 256 KB RAM using `deep65280.img` and `disk0.dsk`.

#### Option A: Using Boot Mode 8 (`mode8.tcl`)
Boot mode 8 is configured in [`misc/pc/mode8.tcl`](file:///home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/mode8.tcl) and installed on board LittleFS (`/rc/mode8.tcl`):
```tcl
set Config(engine) "deep65280"
set Config(flat_img) "/pc/deep65280.img"
set Config(drive0) "/pc/disk0.dsk"
set Config(drive1) ""
menu store Config
bye
```

Launch with a single command:
```bash
sh tether.sh -bootmode=8
```

#### Option B: Interactive Launch via Tcl Shell
1. Launch Tether:
   ```bash
   sh tether.sh
   ```
2. In the `TCL>` prompt, configure Deep65280 and type `bye`:
   ```tcl
   TCL> set Config(engine) "deep65280"
   TCL> set Config(flat_img) "/pc/deep65280.img"
   TCL> set Config(drive0) "/pc/disk0.dsk"
   TCL> set Config(drive1) ""
   TCL> menu store Config
   TCL> bye
   ```
3. The board outputs:
   ```text
   [BYE — starting 6309]
   [deep65280: opened drive 0: /pc/disk0.dsk]
   [deep65280: loading /pc/deep65280.img into 256KB RAM...]
   [deep65280: loaded 8192 + 57344 = 65536 bytes into RAM (256KB)]
   [bye: launching core1...]
   [bye: HALT released, 6309 booting]
   NitrOS-9/6809 Level 2 DEEP65280
   Shell
   OS9:
   ```

### Engine 4: gep9 (Hatvan 6309 OS)

`gep9` executes the Hatvan operating system using demand-paged 256-byte page frames from the 256 KB SRAM pool, hardware `TaskFuse` task switching, and direct sector DMA across up to four OS-9 RBF disks (`/d0` through `/d3`).

Hatvan OS on TFR/911h utilizes multi-task preloading:
- **Task 0**: Monolithic Kernel (`kernel_6809.decb`), reset vector entry point `cstart` at `$1000`.
- **Task 1**: RBF Disk Driver (`rbf_6809.decb`), initialized by kernel via `TaskFuse=1` and `RTI`.
- **Task 2**: PROCFS Driver (`procfs_6809.decb`), initialized via `TaskFuse=2` and `RTI`.
- **Task 3+**: Dynamic user processes (the initial user shell `gsh` and child processes).
- **Drive 0**: OS-9 RBF Filesystem (`hatvan0.dsk` / `disk0.dsk`).

#### Option A: Dedicated Launcher Script (Recommended)
A dedicated shell script [`run_gep9.sh`](file:///home/strick/modoc/coco-shelf/tfr9/v4/run_gep9.sh) is provided in the repository root:
```bash
./run_gep9.sh
```
This automatically invokes Tether mounting `misc/pc` as `/pc` with `-bootmode=80`.

#### Option B: Automated Boot Mode (`-bootmode=80`)
Launch directly via Tether:
```bash
sh tether.sh -bootmode=80
```
This loads and executes [`misc/pc/mode80.tcl`](file:///home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/mode80.tcl), which sources [`misc/pc/boot_gep9.tcl`](file:///home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/boot_gep9.tcl):
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

#### Option C: Interactive Launch via Tcl Shell
1. Launch Tether into Phase 1:
   ```bash
   sh tether.sh
   ```
2. At the `TCL>` prompt, configure `gep9`:
   ```tcl
   TCL> set Config(engine) "gep9"
   TCL> set Config(flat_img) "/pc/kernel_6809.decb"
   TCL> set Config(task1) "/pc/rbf_6809.decb"
   TCL> set Config(task2) "/pc/procfs_6809.decb"
   TCL> set Config(drive0) "/pc/hatvan0.dsk"
   TCL> menu store Config
   TCL> bye
   ```

#### Interactive Hatvan Session Transcript
Upon release of the physical 6309E CPU, the kernel initializes Task 0, boots Task 1 (RBF) and Task 2 (PROCFS), mounts `/d0`, and spawns the interactive shell:
```text
[BYE — starting 6309]
[gep9: opened drive 0: /pc/hatvan0.dsk]
[gep9: loading /pc/kernel_6809.decb into Task 0...]
[gep9: parsing Extended DECB format...]
[gep9: DECB execution address $1000 set in reset vector]
[gep9: Task 0 image load complete]
[gep9: loading /pc/rbf_6809.decb into Task 1...]
[gep9: loading /pc/procfs_6809.decb into Task 2...]
[gep9: IO and MMU initialized]
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
$ help
Commands: help gecho gcat gdir exit
$ gecho Hello TFR911H Hitachi 6309!
Hello TFR911H Hitachi 6309!
$ gdir
Cmds
test_expr.sh
test_while.sh
triangle.sh
$ exit
[Target shutdown: status=0]
```
Entering `exit` causes the shell to terminate, which triggers clean target shutdown via `$FF05` (`Exit.Code`).

---

## 6. Interactive Shell vs Automated Verification (`stop 0`)

On fresh builds of NitrOS-9 disk images (`disk0.dsk`), the startup script at `/startup` may be configured for automated regression testing, concluding with `stop 0` (which halts the CPU and instructs Tether to exit).

### Disabling `stop 0` for an Interactive Session
To keep the shell open for interactive commands (`mdir -e`, `dir`, `mfree`), modify or replace `/startup` using the `os9` (Toolshed) utility:

1. **Inspect current startup**:
   ```bash
   os9 list /tmp/pc/disk0.dsk,startup
   ```

2. **Replace with standard non-paging test startup**:
   ```bash
   echo "tmode </term pau=0" | os9 copy -l -r /dev/stdin /tmp/pc/disk0.dsk,/startup
   ```

3. **Or replace with an empty file**:
   ```bash
   os9 copy -l -r /dev/null /tmp/pc/disk0.dsk,/startup
   ```

When NitrOS-9 boots, `SysGo` processes `startup`, sees end-of-file, and loops to spawn the interactive user shell on `/term`:
```text
OS9: mdir -e
   Module Directory at 00:00:00
Block Offset Size Typ Rev Attr  Use Module Name
----- ------ ---- --- --- ---- ---- ------------
  3C   19DB   39F  11   0 r...    0 Dir
  3C   1D7A   139  11   0 r...    0 Setime
  3D    256   778  D1   0 r...    2 SCF
  3D    9CE   405  E1   0 r...    2 sc6850
  3D    F20    3F  F1   0 r...    2 Term
  3D    F5F   602  11   0 r...    1 Shell
  3E     74    EC  E1   2 r...    1 EmuDsk
  3E    160   187  11   3 r...    0 SysGo
  3E    2E7  12F2  D1   3 r...    1 RBF
  3E   15D9   A27  C1   6 r...    1 IOMan
  3F    125   CDB  C1   0 r...    1 KrnP2
  3F    E00  10DF  C0   0 r...    0 Krn
```
