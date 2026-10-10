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
