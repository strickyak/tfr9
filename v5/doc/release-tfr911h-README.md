# TFR/911H Pre-Compiled Release (`v5`)

This archive contains everything needed to install, upgrade, and run the **TFR/911H** single-board computer (`RP2350B` + `HD63C09E` / `MC6809E`) from **Linux**, **macOS**, or **Windows** — no compiler, Pico SDK, or Go toolchain required.

---

## Contents of This Release

```text
tfr911h-release/
├── README.md                     # This guide (Cold Install, Upgrade Install, Guest Systems)
├── io-map.md                     # Hardware I/O address map reference
├── tfr911h.uf2                   # TFR/911H RP2350B firmware image
├── tether.sh                     # Portable shell wrapper for bin/tether.<os>-<arch>.exe
├── cold_install_tfr911h.sh       # Step-2 helper for Cold Install (reformat + write board label)
├── upgrade_tfr911h.sh            # Helper for Upgrade Install (preserves flash storage & label)
├── run_tfr911h_turbo9os.sh       # Launch built-in Turbo9OS + Basic09
├── run_tfr911h_nitros9_l1.sh     # Launch NitrOS-9 Level 1 (flat65280, 64 KB)
├── run_tfr911h_nitros9_l2.sh     # Launch NitrOS-9 Level 2 (deep65280, 256 KB MMU)
├── run_tfr911h_jamesbrown.sh     # Launch NitrOS-9 Level 2 (jamesbrown: FujiNet + Unix tools + games + Basic09)
├── run_tfr911h_gep9.sh           # Launch Hatvan OS (gep9, 1-level EMBIGGEN MMU)
├── run_tfr911h_gep9big.sh        # Launch Hatvan OS (gep9big, 2-level EMBIGGEN MMU)
├── run_tfr911h_fuzix.sh          # Launch Fuzix (multicomp09, 256 KB banked RAM)
├── run_tfr911h_turbolab.sh       # Launch TurboLab 4-phase cycle-accurate bus tracer
├── bin/                          # Statically compiled tether binaries for 8 OS/CPU targets:
│   ├── tether.linux-amd64.exe    #   Linux x86_64
│   ├── tether.linux-386.exe      #   Linux 32-bit x86
│   ├── tether.linux-arm-7.exe    #   Linux 32-bit ARMv7 (Raspberry Pi OS 32-bit)
│   ├── tether.linux-arm64.exe    #   Linux 64-bit ARM64 (Raspberry Pi OS 64-bit, etc.)
│   ├── tether.mac-arm64.exe      #   macOS Apple Silicon (M1/M2/M3/M4)
│   ├── tether.mac-amd64.exe      #   macOS Intel x86_64
│   ├── tether.win-amd64.exe      #   Windows 64-bit x86_64
│   └── tether.win-386.exe        #   Windows 32-bit x86
├── scripts/                      # Helper scripts (find-tether.sh, find-board.sh, FujiNet diag)
└── images/                       # Clean factory kernel & disk images for all guest OSes
```

### Host OS Notes (Read Once)

* **Linux USB serial permissions:** Ensure your user belongs to the serial group (`dialout` on Debian/Ubuntu, `uucp` on Arch/Fedora) so `/dev/ttyACM*` opens without `sudo`:
  ```sh
  sudo usermod -aG dialout "$USER"   # log out and back in once after adding
  ```
* **macOS Gatekeeper quarantine:** If macOS blocks unsigned binaries downloaded in a `.zip`, clear the quarantine attribute once after unzipping:
  ```sh
  xattr -dr com.apple.quarantine .
  ```
* **Windows PowerShell / CMD:** Pass flags with a space instead of `=` and quote values containing commas (PowerShell splits unquoted `=` and `,`), for example:
  ```powershell
  .\bin\tether.win-amd64.exe -wire COM5 -just-get-label
  ```

---

## Understanding the 16 MB Flash Layout

The TFR/911H carries 16 MB of onboard QSPI flash divided into three independent regions:

| Flash Offset | XIP Address | Size | Purpose |
|---|---|---|---|
| `0x000000 .. 0x3FFFFF` | `0x10000000 .. 0x103FFFFF` | 4 MB | **Firmware (`tfr911h.uf2`)** — RP2350B code + built-in Turbo9OS/Basic09 ROM |
| `0x400000 .. 0xEFFFFF` | `0x10400000 .. 0x10EFFFFF` | 11 MB | **LittleFS Flash Storage (`/`)** — persistent on-board files & `/rc` scripts |
| `0xFFF000 .. 0xFFFFFF` | `0x10FFF000 .. 0x10FFFFFF` | 4 KB | **Board Flash Label** — `p=1,b=tfr9,v=911h,mhz=250,id=...,name=...` |

Choose **Case 1 (Cold Install)** or **Case 2 (Upgrade Install)** below depending on the current state of your board.

---

## Case 1: Cold Install (New Board, Blank Flash, or Foreign Flash Contents)

Use **Cold Install** when your TFR/911H has never been programmed, has uninitialized flash, or previously held unrelated firmware/files. Cold Install flashes `tfr911h.uf2`, formats a clean 11 MB LittleFS filesystem, and writes the **Board Flash Label** so scripts can automatically find your board by type (`b=tfr9 v=911h`), ID (`id=...`), or name (`name=...`).

### Step 1 — Load `tfr911h.uf2` via BOOTSEL (One Button Press)

1. Plug a USB-C **data** cable from your computer into the USB-C connector on the TFR/911H.
2. Locate the two small buttons near the USB-C connector: **RESET** and **FLASH** (BOOTSEL).
3. **Hold down `FLASH`**, press and release **`RESET`** (while keeping `FLASH` held), then **release `FLASH`**.
4. Within 1–2 seconds, a USB Mass Storage drive named **`RP2350`** mounts on your computer:
   * **Linux:** `/media/$USER/RP2350` or `/run/media/$USER/RP2350`
   * **macOS:** `/Volumes/RP2350`
   * **Windows:** a new drive letter (e.g. `D:\` or `E:\`) labeled `RP2350` containing `INFO_UF2.TXT`
5. Copy `tfr911h.uf2` onto the `RP2350` drive:
   ```sh
   # Linux example:
   cp tfr911h.uf2 /media/$USER/RP2350/

   # macOS example:
   cp tfr911h.uf2 /Volumes/RP2350/
   ```
   *(On Windows, macOS Finder, or Linux desktop, you can also drag-and-drop `tfr911h.uf2` onto `RP2350`.)*
6. After a few seconds, `RP2350` automatically ejects, the TFR/911H blinks its LED 3 times, and a USB serial port appears (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS, `COMx` in Windows Device Manager).

### Step 2 — Format Flash Storage & Write the Board Label

Pick a short identifier (`id`, e.g. `t1`) and optional friendly `name` (e.g. `Desk911` — ASCII, no spaces, commas, or `=`).

#### On Linux or macOS (Automated Script)

```sh
./cold_install_tfr911h.sh --id=t1 --name=Desk911
```

*(If you have multiple Pico/RP2350 devices plugged in at the same time, the script will list them and ask you to pass `--wire=/dev/...` so it never touches the wrong board.)*

#### On Windows PowerShell / CMD (or Manual Commands on Any OS)

Find the board's `COM` port in Windows Device Manager (under *Ports (COM & LPT)*, e.g. `COM5`), then run:

```powershell
.\bin\tether.win-amd64.exe -n -bind "" -wire COM5 -just-restart 27
.\bin\tether.win-amd64.exe -n -bind "" -wire COM5 -just-reformat
.\bin\tether.win-amd64.exe -n -bind "" -wire COM5 -just-set-label "p=1,b=tfr9,v=911h,mhz=250,id=t1,name=Desk911"
.\bin\tether.win-amd64.exe -n -bind "" -wire COM5 -just-get-label
```

> **Why write the label?**
> USB serial port names (`/dev/ttyACM0`, `/dev/ttyACM1`, `COM5`) can change across reboots or when multiple boards are plugged in. Every `run_tfr911h_*.sh` script calls `./scripts/find-board.sh b=tfr9 v=911h` to locate your TFR/911H automatically by its flash label!
> * List all attached labeled boards: `./scripts/find-board.sh`
> * Find a specific board by ID or name: `./scripts/find-board.sh id=t1` or `./scripts/find-board.sh name=Desk911`
> * *(Optional offline UF2 method: `./tether.sh -just-label-data="p=1,b=tfr9,v=911h,mhz=250,id=t1" -just-label-filename=_metadata.uf2` creates a 512-byte UF2 targeting `0x10FFF000` if you ever want to flash a label via BOOTSEL.)*

---

## Case 2: Upgrade Install (Board Already Has Label & Files in Flash)

Use **Upgrade Install** when your TFR/911H is already set up and you want to update `tfr911h.uf2` **without** losing your LittleFS files (`4..15 MB`) or your Board Flash Label (`0x10FFF000`).

Flashing `tfr911h.uf2` **only** writes to the first 4 MB firmware region (`0x10000000 .. 0x103FFFFF`). Do **not** run `-just-reformat` during an upgrade.

### Option A — Button-Free Upgrade (Linux / macOS with Desktop Automount)

```sh
./upgrade_tfr911h.sh
```

*(If you have multiple TFR/911H boards attached, pass `--id=t1` or `--wire=/dev/...`.)*
This locates your board by label, commands it into BOOTSEL mode, copies `tfr911h.uf2` to the mounted `RP2350` volume, waits for it to reboot, and verifies your label is intact.

### Option B — Manual BOOTSEL Upgrade (Windows, macOS, or Headless Linux)

1. Hold down **`FLASH`**, press and release **`RESET`**, then release **`FLASH`**.
2. Copy the new `tfr911h.uf2` onto the `RP2350` USB drive.
3. Wait ~3 seconds for the board to reboot. Your LittleFS files and `0x10FFF000` Flash Label remain completely intact!

### Upgrade Note for `v4` Users with Custom `/rc` Scripts in Flash

In `v5`, the legacy `v4` `set Config(engine) ...` Tcl variable was replaced by the unified grammar:

```tcl
on tfr911h run <os> [for <target>] [with ...]
```

All `run_tfr911h_*.sh` scripts in this release use bootmode numbers `9010..9050` so old `/rc/mode1.tcl .. /rc/mode90.tcl` files will not interfere. However, if you previously created `/rc/init.tcl` in Flash under `v4`, it runs automatically on startup unless bootmode `27` is used. To inspect or update `/rc` safely without running startup scripts:

```sh
./tether.sh -bootmode=27
TCL> ls -l /rc
```

---

## Running the Bundled Guest Operating Systems & Tools

Once installed, launch any guest OS directly from this folder:

| Script | Guest System / Tool | Notes |
|---|---|---|
| `./run_tfr911h_turbo9os.sh` | **Turbo9OS + Basic09** | Built into firmware ROM; try `mdir` or `basic09` |
| `./run_tfr911h_nitros9_l1.sh` | **NitrOS-9 Level 1 (`flat65280`)** | 64 KB flat RAM + `flat0.dsk` on EmuDsk |
| `./run_tfr911h_nitros9_l2.sh` | **NitrOS-9 Level 2 (`deep65280`)** | 256 KB MMU RAM + `deep0.dsk` on EmuDsk |
| `./run_tfr911h_jamesbrown.sh` | **NitrOS-9 Level 2 (`jamesbrown`)** | 256 KB GIME MMU, Unix tools (`ls`, `vi`, `man unix`), games (`tetris`), `basic09`, and FujiNet (`fnstatus`, `fnwx`, ...) |
| `./run_tfr911h_gep9.sh` | **Hatvan OS (`gep9`)** | Hitachi 6309E + 1-level EMBIGGEN MMU |
| `./run_tfr911h_gep9big.sh` | **Hatvan OS (`gep9big`)** | Hitachi 6309E + 2-level EMBIGGEN MMU |
| `./run_tfr911h_fuzix.sh` | **Fuzix (`multicomp09`)** | UNIX-like OS in 256 KB banked RAM (`root` login at prompt) |
| `./run_tfr911h_turbolab.sh` | **TurboLab Cycle Tracer** | Example: `./run_tfr911h_turbolab.sh WITH trigger = r/0xFFFE WITH trace_count = 100 -- -n` |
| `./tether.sh` | **Interactive `TCL>` Shell** | Manage on-board LittleFS (`ls`, `cp`, `df`, `cat`, `edit`), inspect `ports list`, or type `bye` |

### Useful Tips

* **Persistent disk changes & factory reset:** On first run, each `run_tfr911h_*.sh` script copies its clean disk image from `images/<guest>/` into `build/demo-pc/<guest>/`. Changes you save inside the guest OS persist in `build/demo-pc/<guest>/`. Delete `build/demo-pc/<guest>/` at any time to revert to a fresh factory disk!
* **Exiting `tether`:**
  * In most guests, press **`Ctrl-C`** to exit `tether` back to your host shell.
  * In `run_tfr911h_jamesbrown.sh`, `Ctrl-C` is forwarded into NitrOS-9 (`-pass_ctrl_c`); press **`Ctrl-]`** to exit `tether`.
* **Targeting one board among several:** Pass `-wire=/dev/...` (or set `WIRE=$(./scripts/find-board.sh id=t1)`) to any `run_tfr911h_*.sh` script.
* **FujiNet Hat diagnostics:** If your TFR/911H wears a FujiNet ESP32 hat, run `./scripts/diagnose_fujinet.sh --tfr911h` to check pin wiring, 921,600-baud UART link health, and WiFi status.
* **Terminal reset:** If `tether` is ever killed abruptly and leaves your Unix terminal without echo, type `Ctrl-J` `reset` `Ctrl-J`.
