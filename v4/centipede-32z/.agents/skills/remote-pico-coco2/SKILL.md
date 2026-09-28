---
name: remote-pico-coco2
description: >-
  Comprehensive guide and reference for remote operations on the Centipede RP2350 (Pico) and
  CoCo 2/3. Covers interactive and scripted Tcl commands, LittleFS and virtual filesystem (VFS)
  features (transparent zip/DECB/OS-9 images, /pc host mount), remote flashing, rebooting,
  command injection, keyboard typing, RAM/screen capture, and remote file transfers via tether.
---

# Remote Use of the Pico and CoCo 2/3

This guide provides a comprehensive manual and command reference for remotely managing, inspecting, scripting, and operating the Centipede RP2350 (Pico) board and its host Color Computer 2/3 over USB using `tether`.

---

## 1. System Architecture & Dual-Phase Model

The Centipede hardware plugs into the Color Computer's cartridge slot. The RP2350B microcontroller acts as a smart cartridge supervisor, bus emulator, memory controller, and console manager.

### The Two Operational Phases

The system operates in one of two distinct operational phases:

```
+-------------------------------------------------------------------+
|                        T Phase (Tcl Console)                      |
|  - 6809 CPU is held in RESET / HALT                               |
|  - RP2350 runs an interactive Tcl 6.7c REPL over USB CDC          |
|  - Full access to LittleFS flash, /pc host bridge, VFS archives   |
|  - Hardware configuration via 'menu' and 'centipede' commands     |
|  - Ends when user or script executes 'bye'                        |
+-------------------------------------------------------------------+
                                  |
                                  v 'bye' command unhalts 6809
+-------------------------------------------------------------------+
|                        U Phase (6809 User Mode)                   |
|  - 6809 CPU runs native code (Color BASIC, OS-9, Forth, etc.)     |
|  - RP2350 emulates RAM, ROM, SAM, VDG, GIME, and floppy hardware  |
|  - Real-time bus cycle tracing streamed to host tether            |
|  - Keystrokes injected over USB via PIA keyboard scanner          |
|  - Video screens rendered on host web UI or captured via RPC      |
+-------------------------------------------------------------------+
```

---

## 2. Remote Access Modes via `tether`

The host communicates with the Pico using the `tether` tool in [`tether/`](../../../tether/). All operations can be performed from `/home/strick/modoc/coco-shelf/tfr9/v4/centipede-32z/`.

### Summary of Host-Side Access Methods

| Method | Command Syntax | When to Use |
| :--- | :--- | :--- |
| **Interactive Console** | `go run ./tether/` | Interactive sessions with web UI, live keyboard, and streaming bus trace |
| **Cooked Line Mode** | `echo "..." \| go run ./tether/ -cooked` | Scripted multi-line terminal input without `stty cbreak`, exits cleanly on EOF |
| **Quick Command Inject** | `go run ./tether/ -quick-inject="<tcl>"` | Non-interactive single Tcl command execution over PicoRPC; returns command output |
| **Quick Restart** | `go run ./tether/ -quick-restart=<mode>` | Remote reboot of Pico and CoCo into specified boot mode |
| **Quick Ping** | `go run ./tether/ -quick-ping=<N>` | Remote liveness / connectivity check |
| **Quick Reflash** | `go run ./tether/ -quick-reflash=<uf2>` | Reboots Pico into BOOTSEL and copies UF2 binary |
| **Quick Upload** | `go run ./tether/ -quick-upload=<zip\|target=src>` | Remote file/archive upload to LittleFS flash |
| **Quick Type** | `go run ./tether/ -quick-type="<keys>"` | Inject keystrokes into running CoCo via keyboard matrix |
| **Quick Screen Capture**| `go run ./tether/ -quick-get-text` | Read live RAM and dump decoded VDG text screen |

---

## 3. Entering the Tcl Shell (Mode 27)

During startup, the firmware reads `boot_mode`. By default, boot scripts in `/rc/` configure hardware and launch the 6809 CPU (`bye`).

### Mode 27 (Break / REPL Mode)

Boot mode **27** corresponds to ASCII 27 (`<ESC>` / `BREAK`). In mode 27, the firmware:
1. **Skips** sourcing `/rc/init.tcl` and `/rc/mode*.tcl`.
2. Leaves the 6809 CPU halted.
3. Enters the interactive Tcl REPL immediately.

### Restarting into Mode 27 from the Host

```bash
# Reboot Pico into Mode 27
go run ./tether/ -quick-restart=27

# Verify responsiveness
go run ./tether/ -quick-ping 88
```

---

## 4. Executing Tcl Commands Remotely

Once the Pico is in Mode 27, you can run Tcl and filesystem commands from the host using either **Quick Inject** or **Cooked Mode**.

### Method A: Single-Shot Command Injection (`-quick-inject`)

`-quick-inject` sends an RPC request to the Pico's Tcl interpreter, waits for evaluation to complete, prints the result string to stdout, and exits.

> [!TIP]
> Use `-omit_stderr -bind=""` to suppress tether debug logging and avoid starting the web server.

```bash
# Directory listing
go run ./tether/ -omit_stderr -bind="" -quick-inject="ls -l /"

# Filesystem usage
go run ./tether/ -omit_stderr -bind="" -quick-inject="df"

# Read bootmode
go run ./tether/ -omit_stderr -bind="" -quick-inject="centipede bootmode"

# Read RTC clock
go run ./tether/ -omit_stderr -bind="" -quick-inject="clock"
```

### Method B: Cooked Terminal Mode (`-cooked`)

`-cooked` reads lines from standard input and sends them character-by-character over USB serial. When standard input reaches EOF, tether waits for `-cooked-drain` (default 2s) and exits cleanly.

```bash
# Run a command and exit on EOF
echo "ls -l /" | go run ./tether/ -cooked -omit_stderr -bind="" -cooked-drain 2s

# Run multiple commands sequentially
cat << 'EOF' | go run ./tether/ -cooked -omit_stderr -bind="" -cooked-drain 3s
pwd
cd /rc
ls -l
EOF
```

---

## 5. Pico Filesystem & VFS Architecture

The Pico firmware implements a full Virtual Filesystem (VFS) backed by LittleFS on SPI flash, transparent archive mounting, and a remote RPC bridge to the host PC.

### Storage Partitioning
- **Filesystem**: LittleFS v2
- **Capacity**: ~11.2 MB (2,816 blocks of 4,096 bytes each)
- **Root Directories**:
  - `/boot`: System boot images and ROMs.
  - `/rc`: Startup Tcl scripts (`init.tcl`, `mode1.tcl`, `mode90.tcl`, etc.).
  - `/fd`: On-board floppy disk images (`f0.dsk` .. `f3.dsk`).
  - `/fresh`: Pristine/original distribution files.
  - `/pc`: Virtual mount point bridging to host PC.

### Unified VFS Node System

Paths in VFS seamlessly resolve different storage providers:

```
/ (Root)
├── LittleFS Flash Nodes  (/rc, /boot, /fd, /fresh, ...)
├── /pc (TetherFsNode)    --> Proxied over USB to host tether directory
└── Archive Mounting (!)  --> Transparent inspection of container formats
    ├── *.zip!/           --> Zip archive filesystem
    ├── *.dsk!/ (DECB)    --> DECB disk image filesystem
    └── *.dsk!/ (OS-9)    --> OS-9 disk image filesystem
```

### The Exclamation Mark (`!`) Archive Operator

Appending `!` to a container file allows you to navigate into it like a directory:

```tcl
# List files inside a ZIP archive on the host PC:
ls -l /pc/archive.zip!/

# List files inside an OS-9 disk image on the Pico:
ls -l /fd/f0.dsk!/

# View a BASIC program directly inside a DECB disk image:
cat /fd/f0.dsk!/DEMO.BAS
```

### Line-Ending Translators

VFS provides on-the-fly line ending conversion by suffixing filenames:
- `:cr` or `!cr` — Translates newlines to carriage returns (`\r`, CoCo native).
- `:lf` or `!lf` — Translates newlines to linefeeds (`\n`, Unix native).
- `:hex` or `!hex` — Presents binary file contents as Intel HEX records.

---

## 6. Complete Tcl & Shell Command Reference

### Directory & Navigation Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `pwd` | None | Print current working directory. |
| `cd` | `[dir]` | Change directory (defaults to `/`). |
| `ls` | `[-a] [-l] [-d] [-r] [path...]` | List directory contents (`-l`: long, `-a`: all, `-d`: dir only, `-r`: reverse). |
| `find` | `[path]` | Recursively list all files and subdirectories. |

### File Manipulation Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `cat` | `file...` | Print contents of one or more files to stdout. |
| `head` | `[-n N] file...` | Display first $N$ lines of a file (default 10). |
| `tail` | `[-n N] file...` | Display last $N$ lines of a file (default 10). |
| `cp` | `src... dst` | Copy files or directories. |
| `mv` | `src... dst` | Move or rename files/directories. |
| `rm` | `[-r] file...` | Delete files (use `-r` for recursive directory removal). |
| `mkdir` | `dir...` | Create new directories. |
| `rmdir` | `dir...` | Remove empty directories. |
| `wc` | `file...` | Print line, word, and byte counts. |
| `grep` | `pattern file...` | Search for regular expression matches in files. |

### Binary & Checksum Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `hd` | `file...` | Hexdump file contents (address, hex bytes, ASCII). |
| `md5` | `file...` | Compute and display MD5 checksum of files. |

### Filesystem Storage & Shell Features

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `df` | None | Report total, used, and free filesystem blocks and kilobytes. |
| `du` | `[path]` | Display disk space usage for path. |
| `fs` | `cmd [args...] [>file]` | Shell execution engine supporting globbing (`*`, `?`) and output redirection (`>`). |

> [!NOTE]
> The Tcl REPL automatically wraps commands in `fs` unless they contain special Tcl punctuation (`;`, `[`, `{`, `"`). This allows Unix-like syntax such as `ls *.tcl > files.txt`.

### Synchronization & Archive Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `rsync-a` | `srcDir destDir` | Recursively synchronize a directory tree. Supports archives as source (e.g. `rsync-a /pc/update.zip! /`). |
| `zip` | `archive.zip file...` | Create a new zip archive containing the specified files. |
| `lzip` | `archive.zip` | List files contained in a zip archive. |

### Text Editing & Scripts

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `edit` | `file` | Full-screen visual text editor running in terminal console. |
| `source` | `script.tcl` | Read and evaluate Tcl commands from a file. |

### Centipede Hardware Control (`centipede`)

| Subcommand | Arguments | Description |
| :--- | :--- | :--- |
| `centipede bootmode` | None | Returns the active numeric boot mode (e.g. `27`, `90`, `1`). |
| `centipede restart` | None | Hard restart of the RP2350 microcontroller. |
| `centipede reflash` | None | Reboot RP2350 directly into USB BOOTSEL mass storage mode. |
| `centipede stacksize` | None | Returns used and free coroutine stack bytes. |
| `centipede type` | `<string>` | Queues a keystroke string into the hardware keyboard injector. |
| `centipede flash-filesystem-stats` | None | Dumps low-level LittleFS block and wear-leveling metrics. |
| `centipede reformat-flash-filesystem` | `-force` | Completely reformats the LittleFS partition on flash. |

### Hardware Configuration & Launching CoCo (`menu`, `bye`)

The `menu` command manages the `Config` array used to set up Centipede before launching:

```tcl
menu fetch Config           ;# Load active settings into Tcl array 'Config'
set Config(become_coco3) 1  ;# 1 = CoCo 3 (GIME, 128K), 0 = CoCo 2
set Config(ram_64k) 1       ;# Inject 64KB RAM
set Config(rom_disk11) 1    ;# Inject Disk Extended Color BASIC 1.1 ROM
set Config(floppy_fd) 1     ;# Map floppies from /fd/f*.dsk
set Config(floppy_pc) 0     ;# Map floppies from /pc/f*.dsk
set Config(trace_writes) 1  ;# Stream bus write cycles to tether
set Config(trace_reads) 0   ;# Stream bus read cycles to tether
menu store Config          ;# Apply settings to hardware registers
bye                        ;# Exit Tcl REPL, unhalt 6809, and boot CoCo!
```

---

## 7. Host-Side File Transfer & Serving (`/pc`)

`tether` allows the host PC to serve files directly to the Pico over USB RPC without any SD cards.

### The Virtual `/pc` Filesystem

When `tether` runs, it hosts a local directory (specified by `-pc`, defaults to `/tmp/pc`):

```bash
# Place files on the host
mkdir -p /tmp/pc
cp my_disk.dsk /tmp/pc/f0.dsk
cp update.bas /tmp/pc/test.bas

# On the Pico (via quick-inject or cooked mode), access them immediately:
go run ./tether/ -omit_stderr -bind="" -quick-inject="cp /pc/test.bas /test.bas"
```

### Remote Zip Extraction (`-quick-upload`)

To upload an entire directory tree or package of files to the Pico flash in a single operation:

```bash
# Package files on host
zip -r myfiles.zip boot/ rc/ fd/

# Upload and extract to Pico root:
go run ./tether/ -quick-upload=myfiles.zip
```
Under the hood, tether places `myfiles.zip` in `/pc`, connects to the Pico, and injects `rsync-a /pc/update.zip! /`.

### Uploading Specific File Pairs

```bash
go run ./tether/ -quick-upload="/fd/f0.dsk=my_local_disk.dsk,/test.bas=local.bas"
```

---

## 8. Operating the CoCo 2/3 Remotely (U Phase)

Once the 6809 CPU is booted into Color BASIC or OS-9, you can drive it remotely from the host.

### 1. Keystroke Injection (`-quick-type`)

Inject keystrokes directly into the CoCo keyboard matrix scanner via RPC:

```bash
# Type a BASIC command and press Enter (\r)
go run ./tether/ -quick-type="PRINT 6*7\r"

# Load and run a program at 10 chars/sec
go run ./tether/ -cps=10.0 -quick-type="LOAD\"GAME\"\rRUN\r"
```

### 2. Dumping Live Video RAM (`-quick-get-text`)

Read the MC6847 32x16 text screen from live Pico RAM (`0x0400`–`0x05FF`) and print decoded ASCII text to stdout:

```bash
go run ./tether/ -quick-get-text
```

### 3. Capturing Graphics Screens to PNG

Save live high-resolution graphics directly to PNG image files on the host:

```bash
# VDG PMODE graphics: -quick-get-pmode="mode,page,colorset,output.png"
go run ./tether/ -quick-get-pmode="4,1,1,pmode4.png"

# GIME HSCREEN graphics: -quick-get-hscreen="mode,output.png"
# (mode 2 = 320x192 16-color, mode 4 = 640x192 4-color)
go run ./tether/ -quick-get-hscreen="2,hscreen2.png"
```

### 4. Fetching Full 64KB RAM (`-quick-get-ram`)

Fetch the entire 64KB CoCo address space over USB using 256-byte chunked RPC:

```bash
go run ./tether/ -quick-get-ram=ram_snapshot.bin
```

---

## 9. Practical Step-by-Step Recipes

### Recipe 1: Inspecting Pico Flash Filesystem

```bash
# 1. Ensure Pico is in Mode 27 (Tcl REPL)
go run ./tether/ -quick-restart=27

# 2. Check disk space
go run ./tether/ -omit_stderr -bind="" -quick-inject="df"

# 3. List root files
go run ./tether/ -omit_stderr -bind="" -quick-inject="ls -l /"

# 4. List boot scripts
go run ./tether/ -omit_stderr -bind="" -quick-inject="ls -l /rc"
```

### Recipe 2: Browsing a Disk Image Directly on the Pico

```bash
# Examine files inside an on-board floppy image
go run ./tether/ -omit_stderr -bind="" -quick-inject="ls -l /fd/f0.dsk!/"

# Display the contents of a BASIC program inside the disk
go run ./tether/ -omit_stderr -bind="" -quick-inject="cat /fd/f0.dsk!/README.BAS"
```

### Recipe 3: Cold Booting into Color BASIC and Verifying Output

```bash
# 1. Restart Pico into Mode 27
go run ./tether/ -quick-restart=27

# 2. Exit Tcl REPL and launch CoCo 2 Color BASIC
echo "bye" | go run ./tether/ -cooked -omit_stderr -bind="" -cooked-drain 2s

# 3. Wait 1 second for BASIC cold boot to complete, then inspect text screen
sleep 1
go run ./tether/ -quick-get-text
```

### Recipe 4: Running a Program via Keyboard Injector

```bash
# 1. Inject a BASIC one-liner and execute
go run ./tether/ -quick-type="10 FOR I=1 TO 5:PRINT I*I;:NEXT:PRINT\rRUN\r"

# 2. Wait for execution, then read screen
sleep 1
go run ./tether/ -quick-get-text
```

### Recipe 5: Switching to CoCo 3 Mode Remotely

```bash
# Option A: Quick-restart into Mode 90 (configured for CoCo 3 in /rc/mode90.tcl)
go run ./tether/ -quick-restart=90

# Option B: From Mode 27 via Tcl script
cat << 'EOF' | go run ./tether/ -cooked -omit_stderr -bind="" -cooked-drain 2s
menu fetch Config
set Config(become_coco3) 1
set Config(ram_64k) 1
set Config(rom_disk11) 1
set Config(floppy_fd) 1
set Config(trace_writes) 1
set Config(trace_reads) 0
menu store Config
bye
EOF
```
