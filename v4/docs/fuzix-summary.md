# Fuzix 6809 on TFR/911H SBC and gep9 Emulator

This document summarizes the port, hardware emulation, kernel bugfixes, and operational procedures for running **FUZIX OS 0.5** for the `multicomp09` target on both the **TFR/911H Single Board Computer** (Hitachi 6309E CPU + Raspberry Pi RP2350B co-processor) and the **`gep9` emulator** (`multicomp09fuzix` engine).

---

## 1. System Architecture

The Multicomp09 platform emulated on the TFR/911H hardware and `gep9` emulator consists of:

- **CPU**: Hitachi 6309E executing 6809/6309 instructions at native clock speeds.
- **MMU & Banking ($FFDE..$FFDF)**:
  - 64 KB logical address space split into eight 8 KB slots.
  - Two MMU maps: Map 0 (Kernel) and Map 1 (User / Task).
  - 256 KB physical RAM divided into 32 blocks of 8 KB (pages 0..31):
    - Pages 0–5: Kernel code, initialized data, BSS, and buffers.
    - Pages 6–7: Kernel common & udata page (16 KB bank 6).
    - Pages 8–31: 12 free 16 KB user memory banks (pages 8, 10, 12, ..., 30).
- **Storage ($FFD8..$FFDC)**:
  - Multicomp SPI SD controller using 24-bit LBA ($FFDA..$FFDC) and 512-byte block transfers ($FFD8 data, $FFD9 command/status).
  - Status register protocol: `0x80` = idle, `0xE0` = read data ready, `0xA0` = write buffer ready.
  - Backed by `/pc/disk.img` mounted via Tether VFS RPC.
- **Console UART ($FFD0..$FFD1)**:
  - Motorola 6850 ACIA compatible UART mapped to the USB CDC-ACM console stream.
- **Periodic Timer ($FFDD)**:
  - 50 Hz timer tick driving Fuzix process scheduling and system clock interrupts.

---

## 2. Key Problem Diagnoses & Bug Fixes

### A. Assembly Register Corruption in `fork_copy` (`tricks.s`)
- **Issue**: In `fork_copy`, when calculating the number of 16 KB user banks to copy, `skip@` originally branched past the `pshs cc,a` instruction when the remainder was zero (`U_DATA__U_TOP & 0x3F == 0`). Later, `puls cc,a` popped arbitrary stack values, causing memory corruption and stack misalignment upon fork completion.
- **Fix**: Relocated `skip@` to target `cmpa #4` so `pshs cc,a` always executes unconditionally matching `puls cc,a`.

### B. Out-of-Memory Deadlock in Fork Cascades (`DEFAULT_TOP 0x9000`)
- **Issue**: By default, binaries compiled without an explicit `a_size` memory request (`a_size == 0`) expand to `DEFAULT_TOP = ramtop` (`0xE000`, 56 KB). In Fuzix's 16 KB banked allocator (`bank16k.c`), a 56 KB address space consumes 4 banks (64 KB). With 12 banks available in 256 KB RAM, only 3 processes could exist concurrently. During boot, `/init` (pid 1) forks `/bin/sh` (pid 2), which forks a subshell for `/etc/rc` (pid 3), exhausting all 12 banks (4+4+4=12). When pid 3 attempted to fork `/bin/fsck` (pid 4), `fork()` returned `ENOMEM` (-1) in an infinite retry loop.
- **Fix**: Defined `#define DEFAULT_TOP 0x9000` (48 KB / 3 banks) in `Kernel/platform/platform-multicomp09/config.h`. This ensures standard binaries consume at most 3 banks, allowing 4 concurrent processes and giving `/bin/sh` ample space (12+ KB) for heap and stack.

### C. Makefile Linker Object Discrepancy
- **Issue**: `Kernel/platform/platform-multicomp09/Makefile` compiled `../../mm/bank16k.o` in its `image:` target, but passed `../../bank16k.o` to `m6809-unknown-ld`. Consequently, updates to `bank16k.c` were not reflected in the linked `fuzix.bin`.
- **Fix**: Updated line 56 of `platform-multicomp09/Makefile` to link `../../mm/bank16k.o` and deleted stale root object files.

### D. Multicomp09 SD Write Dispatch in RP2350 Firmware
- **Issue**: In `tfr911h/v4_multicomp09.h`, `mc_write_sd_data` set `mc_sd_ctl = 0x80` before calling `mc_dispatch_sd_cmd(0x01)`. Inside `mc_dispatch_sd_cmd`, an `if (mc_sd_ctl == 0xA0)` check failed, causing sector writes to be discarded and leaving the 6809 spinning indefinitely waiting for controller idle.
- **Fix**: Removed the premature `mc_sd_ctl = 0x80` assignment from `mc_write_sd_data`, allowing `mc_dispatch_sd_cmd` to submit the `FG2BG_MC_SD` write RPC to Core 0 and set `mc_sd_ctl = 0x80` upon verified completion.

---

## 3. How to Run

### A. Running in the `gep9` Emulator
To launch Fuzix on the software emulator:
```bash
/home/strick/github.com/strickyak/hatvan-os/build/gep9 \
  -engine multicomp09fuzix \
  -ram 256k \
  -disk0 /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/disk.img \
  /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/fuzix.bin
```

### B. Running on Physical TFR/911H Hardware
To launch an interactive session on the physical SBC:
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4
./run_fuzix.sh
```

### C. Automated Hardware Test
To run the automated verification suite (`test_fuzix.exp`) which boots Fuzix, runs `fsck`, completes `setdate`, logs in as `root`, and executes `uname -a`, `df`, `ls -la /`, and `sync`:
```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4
./test_fuzix.exp
```
Expected output concludes with:
```text
>>> uname -a completed
>>> df completed
>>> ls completed
>>> sync completed
>>> FUZIX HARDWARE TEST SUCCESSFUL!
```

---

## 4. How to Recompile Fuzix (`platform-multicomp09`) from a Fresh Git Clone

This section documents the end-to-end procedure for compiling the Fuzix kernel and userland filesystem for `multicomp09` starting from a clean upstream repository clone (such as [`/home/strick/codeberg.org/etchedpixels/fuzix.orig`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/)).

### A. Prerequisites & Toolchain Setup

The 6809 toolchain binaries are located in [`/home/strick/modoc/coco-shelf/bin/`](file:///home/strick/modoc/coco-shelf/bin/):
- `m6809-unknown-gcc` (GCC 4.6.4 for 6809)
- `m6809-unknown-as` & `lwasm` (LWTOOLS 6809 assembler)
- `m6809-unknown-ld` (6809 linker)
- `m6809-unknown-ar`

Ensure this directory is at the front of your `PATH`:
```bash
export PATH="/home/strick/modoc/coco-shelf/bin:$PATH"
```

### B. Upstream Base Commit Selection

> [!IMPORTANT]
> In upstream master (August 2026 commits `90816b1cf`..`aee91d86e0`), Alan Cox began transitioning the 6809 architecture to `fcc` (an experimental ACK-based C compiler). That port is work-in-progress and breaks the `gcc6809` toolchain.
>
> When working from a fresh clone of upstream (e.g. `fuzix.orig`), create a working branch rooted at commit [`6fb7b22ab`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/) (the stable FUZIX 0.5 tree where GCC 6809 support is mature and complete):
> ```bash
> cd /path/to/fresh-fuzix-clone
> git checkout -b work-multicomp09-256k 6fb7b22ab
> ```

### C. Essential Code Changes (6 Files)

Apply the following modifications to adapt the multicomp09 target for 256 KB RAM and fix upstream kernel bugs:

1. **[`Kernel/platform/platform-multicomp09/tricks.s`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/Kernel/platform/platform-multicomp09/tricks.s)**:
   Fix stack alignment corruption in `fork_copy`:
   ```diff
   --- a/Kernel/platform/platform-multicomp09/tricks.s
   +++ b/Kernel/platform/platform-multicomp09/tricks.s
   @@ -200,10 +200,10 @@ fork_copy:
    	puls	cc		       ; get remainder's cc (  )
    	beq	skip@		       ; skip round-up if zero
    	inca			       ; round up
   -	cmpa	#4		       ; is 4th bank copied?
   +skip@	cmpa	#4		       ; is 4th bank copied?
    	pshs	cc,a		       ; and put on stack ( 4th?  no )
    	;; copy parent's whole pages to child's
   -skip@	ldx	fork_proc_ptr
   +	ldx	fork_proc_ptr
    	leax	P_TAB__P_PAGE_OFFSET,x ; X = * new process page tables (dest)
   ```

2. **[`Kernel/platform/platform-multicomp09/config.h`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/Kernel/platform/platform-multicomp09/config.h)**:
   Define `DEFAULT_TOP 0x9000` (48 KB / 3 banks) to prevent out-of-memory deadlock during fork cascades:
   ```diff
   --- a/Kernel/platform/platform-multicomp09/config.h
   +++ b/Kernel/platform/platform-multicomp09/config.h
   @@ -39,6 +39,7 @@
    #define TICKSPERSEC 50      /* Ticks per second */
    #define PROGBASE    0x0100  /* also data base */
    #define PROGTOP     0xe000  /* Top of program, base of U_DATA */
   +#define DEFAULT_TOP 0x9000  /* Default process size in 256K RAM (3 banks = 48K) */
    #define PROGLOAD    0x0100  /* ??? */
   ```

3. **[`Kernel/platform/platform-multicomp09/main.c`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/Kernel/platform/platform-multicomp09/main.c)**:
   Configure `pagemap_init` for 256 KB RAM (pages 8..31 step 2 = 12 user banks):
   ```diff
   --- a/Kernel/platform/platform-multicomp09/main.c
   +++ b/Kernel/platform/platform-multicomp09/main.c
   @@ -41,10 +41,11 @@ void do_beep(void)
    void pagemap_init(void)
    {
        int i;
   -    for (i = 10; i < 64; i+=2)
   +    /* Multicomp09 256KB RAM: pages 0-5 are kernel code/data,
   +     * pages 6-7 are kernel common/udata (bank 6).
   +     * Pages 8-31 are free 16KB banks (every other 8KB page).
   +     */
   +    for (i = 8; i < 32; i += 2)
            pagemap_add(i);
        /* add common page last so init gets it */
        pagemap_add(6);
   ```

4. **[`Kernel/platform/platform-multicomp09/multicomp09.s`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/Kernel/platform/platform-multicomp09/multicomp09.s)**:
   Set system RAM size variables to 256 KB:
   ```diff
   --- a/Kernel/platform/platform-multicomp09/multicomp09.s
   +++ b/Kernel/platform/platform-multicomp09/multicomp09.s
   @@ -219,11 +219,10 @@ init_early:
    init_hardware:
   -	ldd 	#512
   +	ldd 	#256
    	std 	_ramsize
   -	ldd 	#512-64
   +	ldd 	#256-64
    	std 	_procmem
   ```

5. **[`Kernel/platform/platform-multicomp09/Makefile`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/Kernel/platform/platform-multicomp09/Makefile)**:
   Fix the linker rule in the `image:` target to link `../../mm/bank16k.o`:
   ```diff
   --- a/Kernel/platform/platform-multicomp09/Makefile
   +++ b/Kernel/platform/platform-multicomp09/Makefile
   @@ -56,1 +56,1 @@ image:
   -	../../mm.o ../../bank16k.o ../../swap.o ../../tty.o ../../devsys.o \
   +	../../mm.o ../../mm/bank16k.o ../../swap.o ../../tty.o ../../devsys.o \
   ```

6. **[`Kernel/platform/platform-multicomp09/target.mk`](file:///home/strick/codeberg.org/etchedpixels/fuzix.orig/Kernel/platform/platform-multicomp09/target.mk)**:
   Export big-endian flag for filesystem generation:
   ```diff
   --- a/Kernel/platform/platform-multicomp09/target.mk
   +++ b/Kernel/platform/platform-multicomp09/target.mk
   @@ -1 +1,2 @@
    export CPU = 6809
   +export ENDIANFLAG = "-X"
   ```

### D. Build Commands

Run the following commands from the repository root:

1. **Build the Kernel**:
   ```bash
   cd Kernel
   make TARGET=multicomp09
   cd ..
   ```
   - **Output file**: `Kernel/fuzix.bin` (Extended DECB format binary).

2. **Build Host Standalone Tools, Libraries, and Userland Applications**:
   ```bash
   make TARGET=multicomp09 stand ltools libs apps
   ```
   - Compiles host tools `Standalone/mkfs` and `Standalone/ucp`.
   - Compiles the 6809 C runtime library (`Library/libs/`).
   - Compiles 6809 userland binaries in `Applications/` (`init`, `sh`, `fsck`, `ls`, `df`, `setdate`, etc.).

3. **Generate Root Filesystem Image (`filesys.img`)**:
   ```bash
   make TARGET=multicomp09 YACC="yacc -y" diskimage
   ```
   - Runs `Standalone/filesystem-src/build-filesystem` to format a 32 MB filesystem and inject the compiled userland binaries.
   - **Output file**: `Images/multicomp09/filesys.img` (32 MB raw filesystem).

4. **Construct Partitioned SD Card Disk Image (`disk.img`)**:
   ```bash
   dd if=Standalone/filesystem-src/parttab.40M of=Images/multicomp09/disk.img bs=40017920 conv=sync
   dd if=Images/multicomp09/filesys.img of=Images/multicomp09/disk.img bs=512 seek=2048 conv=notrunc
   ```
   - **Output file**: `Images/multicomp09/disk.img` (40 MB MBR partitioned disk with partition 1 at LBA 2048).

### E. Deploying Resulting Files to TFR9/V4

Copy the two build artifacts into the tether / emulator staging directories:

| Build Artifact | Destination Path | Purpose |
| :--- | :--- | :--- |
| `Kernel/fuzix.bin` | `/home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/fuzix.bin`<br>(and `/tmp/pc/fuzix.bin`) | Kernel boot image loaded by `boot_fuzix.tcl` or `gep9` |
| `Images/multicomp09/disk.img` | `/home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/disk.img`<br>(and `/tmp/pc/disk.img`) | Root SD block storage attached to Drive 0 (`/dev/hda1`) |

