---
name: centipede-firmware
description: >-
  Guide for compiling the Centipede RP2350B firmware (centipede.uf2), required environment
  variables, build steps, and output artifact locations. Use whenever building, compiling,
  rebuilding, or flashing centipede.uf2.
---

# Compiling the Centipede Firmware (`centipede.uf2`)

This guide explains how to compile the Centipede RP2350B firmware into `centipede.uf2` and where the resulting binaries are located.

---

## 1. Prerequisites & Environment Setup

The build uses the Raspberry Pi Pico SDK targeting the **Solder Party RP2350 Stamp XL** board (`solderparty_rp2350_stamp_xl`, platform `rp2350-arm-s` with RP2350B).

The following environment variables are required:

| Variable | Value | Purpose |
| :--- | :--- | :--- |
| `PICO_SDK_PATH` | `/home/strick/modoc/coco-shelf/pico-sdk` | Path to the Raspberry Pi Pico SDK |
| `PICOTOOL_DIR` | `/home/strick/modoc/coco-shelf` | Path where picotool configurations reside |
| `PATH` | `/home/strick/modoc/coco-shelf/bin:$PATH` | Ensures `picotool` binary is in `PATH` |

All dependencies (LittleFS, MiniZ, Tcl 6.7c, and core headers) reside locally in the [`three/`](../../three/) directory.

---

## 2. Compilation Procedures

Always run compilation commands from the workspace root (`/home/strick/modoc/coco-shelf/tfr9/v4/centipede-32z/`).

### Option A: Using the Makefile (Recommended)

To compile the firmware and automatically stage it into `build/`:

```bash
make FIRMWARE
```

Or to build all workspace components:
```bash
make all
```

### Option B: Manual CMake & Make inside `firmware/build`

If you want to run cmake or make directly:

```bash
mkdir -p firmware/build
cd firmware/build

PATH="/home/strick/modoc/coco-shelf/bin:$PATH" \
PICOTOOL_DIR="/home/strick/modoc/coco-shelf" \
PICO_SDK_PATH="/home/strick/modoc/coco-shelf/pico-sdk" \
cmake ..

PATH="/home/strick/modoc/coco-shelf/bin:$PATH" \
PICOTOOL_DIR="/home/strick/modoc/coco-shelf" \
PICO_SDK_PATH="/home/strick/modoc/coco-shelf/pico-sdk" \
make -j4
```

---

## 3. Output Artifact Locations

After a successful compilation, the output files are located at:

1. **Primary CMake Build Directory**:
   - `firmware/build/centipede.uf2` (The UF2 flash binary for RP2350)
   - `firmware/build/centipede.elf` (ELF executable with debug symbols)
   - `firmware/build/centipede.bin` (Raw binary)
   - `firmware/build/centipede.hex` (Intel HEX format)
   - `firmware/build/centipede.dis` (Assembly disassembly)
   - `firmware/build/centipede.elf.map` (Linker map file)

2. **Top-Level Distribution Directory**:
   - `build/centipede.uf2` (Copied here by `make FIRMWARE` or `make`)

---

## 4. Flashing to the Centipede Board

1. Plug the Centipede USB-C cable into the host PC while holding the **FLASH** button (or hold **FLASH** and momentarily press the **RESTART** button on the Centipede board).
2. The board will enumerate as a USB mass storage device at `/media/$USER/RP*` (e.g., `/media/$USER/RP2340`).
3. Copy `centipede.uf2` to the mounted volume:
   ```bash
   cp -fv build/centipede.uf2 /media/$USER/RP*/.
   ```
   or run:
   ```bash
   make flash
   ```
4. Once copying completes, the drive will automatically unmount and the new firmware will boot.

---

## 5. Verifying & Compiling the `tether` Host Program

The `tether` host tool is written in Go and located in the [`tether/`](../../tether/) directory.

To verify that `tether` compiles without errors and inspect all available command-line flags:

```bash
go run ./tether/ -help
```

Running this command compiles the Go package in `./tether/` on-the-fly and displays the full usage manual and flag definitions (e.g., `-wire`, `-disks`, `-quick-upload`, `-abslists`, `-vdg_text`, etc.).

---

## 6. Quick Ping & Software Reflash via `tether`

Rather than manually pressing hardware buttons to enter BOOTSEL mode, `tether` provides quick-mode flags to ping the firmware via RPC and reflash over USB automatically:

### 1. Test RPC Connection (Ping)
Send a PicoRPC ping with an integer payload to verify communication with the running Centipede:

```bash
go run ./tether/ -quick-ping 42
```
Expected output:
```text
WriteBytes: [16.] { b5 0a 04 70 69 6e 67 11 01 42 04 2a 00 00 00 00 }
quick-ping: OK (value=42)
```

### 2. Software Reflash (`-quick-reflash`)
Command the running Centipede firmware over USB to reboot into BOOTSEL mode, wait for the mass storage drive to mount, and copy the new UF2 firmware:

```bash
go run ./tether/ -quick-reflash firmware/build/centipede.uf2
```
Expected output:
```text
WriteBytes: [13.] { b5 0a 07 72 65 66 6c 61 73 68 11 01 00 }
quick-reflash: Pico entering BOOTSEL mode, waiting 5s for mount...
quick-reflash: cp -v 'firmware/build/centipede.uf2' /media/${USER}/RP*
'firmware/build/centipede.uf2' -> '/media/strick/RP2350/centipede.uf2'
quick-reflash: copy done, waiting 5s for unmount...
quick-reflash: OK
```

### 3. Verify Newly Booted Firmware (Post-Reflash Ping)
Verify that the new firmware rebooted and is responsive:

```bash
go run ./tether/ -quick-ping 63
```
Expected output:
```text
WriteBytes: [16.] { b5 0a 04 70 69 6e 67 11 01 42 04 3f 00 00 00 00 }
quick-ping: OK (value=63)
```

### 4. Rebooting the Pico and CoCo 2 (`-quick-restart`)
You can remotely reboot the Centipede firmware over USB. Because the firmware drives $\overline{\text{RESET}}$ and $\overline{\text{HALT}}$ low on the cartridge bus during its startup sequence, this reboots both the RP2350 (Pico) and, as a side-effect, the host CoCo 2:

```bash
go run ./tether/ -quick-restart 27
```
Expected output:
```text
WriteBytes: [19.] { b5 0a 07 72 65 73 74 61 72 74 11 01 42 04 00 00 00 1b 00 }
quick-restart: OK
```

Then verify that the board has rebooted and is responsive:
```bash
go run ./tether/ -quick-ping 88
```
Expected output:
```text
WriteBytes: [16.] { b5 0a 04 70 69 6e 67 11 01 42 04 58 00 00 00 00 }
quick-ping: OK (value=88)
```

### 5. Dumping Pico RAM (`-quick-get-ram=filename`)
Dump the Pico's raw 64KB `ram[]` array over USB using chunked RPC (transferred 256 bytes per packet to avoid Pico memory pressure) and save it directly as a 65,536-byte raw binary file:

```bash
go run ./tether/ -quick-get-ram=ram.bin
```
Expected output:
```text
quick-get-ram: reading 65536 bytes in 256-byte chunks...
WriteBytes: [21.] { b5 0a 07 67 65 74 2d 72 61 6d 11 ... }
...
quick-get-ram: OK (saved 65536 bytes to ram.bin)
```

---

## 7. Inspecting & Decoding CoCo Video RAM

Once a 64KB dump is saved (e.g., `ram.bin`), you can inspect and render video memory buffers.

### 1. VDG 32x16 Text Mode (`0x0400`–`0x05FF`)
Standard Color BASIC boots into 32x16 text mode at memory address `0x0400`. The MC6847 VDG character generator uses 6-bit uppercase ASCII (`byte & 0x3F`), where:
- Values `0x00`–`0x1F` map to `@`, `A`–`Z`, `[`, `\`, `]`, `^`, `_` (ASCII `value + 0x40`).
- Values `0x20`–`0x3F` map to standard punctuation, digits `0`–`9`, etc. (ASCII `value`).
- Bit 6 (`0x40`) controls inverse video / color attributes depending on mode.

To dump all 16 rows of text:
```bash
python3 -c "
data = open('ram.bin', 'rb').read()
def decode_vdg(b):
    c = b & 0x3F
    return chr(c + 0x40) if c < 0x20 else chr(c)

for row in range(16):
    start = 0x0400 + row * 32
    print(f'{row:2d}: ' + ''.join(decode_vdg(b) for b in data[start:start+32]))
"
```

### 2. VDG PMODE 4 Graphics Mode (`0x0800`–`0x1FFF`)
PMODE 4 is a 256×192 1-bit monochrome graphics mode (6,144 bytes = 6KB) typically located at `0x0800`.
- Each row is 32 bytes (256 pixels across).
- Bits are ordered MSB to LSB (bit 7 is leftmost, bit 0 is rightmost in each byte).

To render an ASCII art preview of the top-left 16×16 pixel block:
```bash
python3 -c "
data = open('ram.bin', 'rb').read()
base = 0x0800
for y in range(16):
    row_bytes = data[base + y * 32 : base + y * 32 + 2] # 2 bytes = 16 pixels
    row_bits = ''.join(f'{b:08b}' for b in row_bytes)
    print(''.join('*' if bit == '1' else '-' for bit in row_bits))
"
```
