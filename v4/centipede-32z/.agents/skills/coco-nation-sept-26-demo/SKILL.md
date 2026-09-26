---
name: coco-nation-sept-26-demo
description: >-
  Guide and exact procedure for performing the CoCo 2 live demonstration for
  CoCo Nation (Sept 26). Reboots Centipede into mode 27, launches native CoCo 2
  Disk Extended Color BASIC 1.1, injects a loop computing cubes (PRINT I*I*I;),
  executes the program, and dumps/decodes the physical 32x16 VDG video text screen.
---

# CoCo Nation Sept 26 Live Demo: CoCo 2 Mode

This skill documents the exact procedure to perform the live CoCo 2 BASIC demo on the Centipede RP2350B hardware attached to a host Color Computer 2.

The demo boots into native **Disk Extended Color BASIC 1.1**, enters a loop calculating and printing the cubes of 1 to 10 horizontally with semicolons (`PRINT I*I*I;`), executes the program, and decodes the resulting 32×16 VDG screen.

---

## 1. Quick Reference Commands

All commands are run from the workspace root:
`/home/strick/modoc/coco-shelf/tfr9/v4/centipede-32z/`

```bash
# 1. Restart Centipede into Mode 27 (Tcl console mode)
go run ./tether/ -quick-restart 27

# 2. Verify connection
go run ./tether/ -quick-ping 88

# 3. Launch native CoCo 2 Color BASIC
echo "bye" | go run ./tether/ -cooked

# 4. Inject program and run (note the $'...' quoting for \r and the ~ pause; defaults to --cps=2.0)
go run ./tether/ -quick-type $'10 FOR I=1 TO 10:PRINT I*I*I;:NEXT\r~RUN\r'

# 5. Wait 21 seconds for keyboard injector (79 actions @ 240/260ms + 1s pause at 2.0 cps)
# (Use schedule tool or wait 21s)

# 6. Dump and decode text screen directly
go run ./tether/ -omit_stderr -quick-get-text=z
# Or with explicit parameters:
# go run ./tether/ -omit_stderr -quick-get-text=0x0400,32,16
```

---

## 2. Step-by-Step Procedure

### Step 1: Remote Restart into Mode 27
Command the Centipede firmware to perform a soft reset and enter mode 27 (interactive Tcl REPL):
```bash
go run ./tether/ -quick-restart 27
```
Verify the board has rebooted and responds on RPC:
```bash
go run ./tether/ -quick-ping 88
```
Expected output:
```text
quick-ping: OK (value=88)
```

### Step 2: Exit Tcl Shell to Launch Color BASIC
In mode 27, `centipede_config.become_coco3` defaults to `false`. Sending `bye` prompts the console driver to reset the 6809 MPU vectors, initialize SAM/VDG registers for 32×16 text mode, and execute `Jump(0xA027)`:
```bash
echo "bye" | go run ./tether/ -cooked
```
The CoCo 2 motherboard immediately cold-starts into **Disk Extended Color BASIC 1.1** and displays the sign-on banner and `OK` prompt.

### Step 3: Inject the Program via Keyboard Injector
Send the program to the running BASIC interpreter via `-quick-type`:
```bash
go run ./tether/ -quick-type $'10 FOR I=1 TO 10:PRINT I*I*I;:NEXT\r~RUN\r'
```

#### Critical Syntax Requirements:
1. **ANSI-C Bash Quoting (`$'...'`)**:
   Standard single quotes `'...\r'` treat `\r` as two literal ASCII characters (`\` and `r`), which the keyboard injector ignores. Using `$'...\r'` ensures byte `0x0D` (Carriage Return / Enter key) is transmitted.
2. **Pause Character (`~`)**:
   The tilde `~` triggers a 1-second pause (`PAUSE_TICKS = 50`) in `keyboard_injector.h`. This ensures Color BASIC has fully parsed and saved line 10 before `RUN\r` is entered as an immediate direct command.
3. **Execution Timing**:
   With `--cps=2.0` (the default), each keystroke is held down for 240 ms (12 ticks) and released for 260 ms (13 ticks) for a total of 500 ms per character (2 characters per second). A 39-character sequence requires 78 key actions plus 1 pause action (~20.5 seconds total).

### Step 4: Wait for Completion
Wait ~21 seconds for keystroke entry and program execution to finish. Do not poll or interrupt the bus while injection is in progress.

### Step 5: Dump and Decode Video Display
Read the 32×16 VDG text screen directly from RAM (`0x0400`–`0x05FF`) and print decoded ASCII to stdout:
```bash
go run ./tether/ -omit_stderr -quick-get-text=z
```
Or specify explicit memory address, width, and height:
```bash
go run ./tether/ -omit_stderr -quick-get-text=0x0400,32,16
```

---

## 3. Expected Screen Output

```text
--- CoCo 2 Screen (0x0400) ---
 0: DISK EXTENDED COLOR BASIC 1.1   
 1: COPYRIGHT (C) 1982 BY TANDY     
 2: UNDER LICENSE FROM MICROSOFT    
 3:                                 
 4: OK                              
 5: 10 FOR I=1 TO 10:PRINT I*I*I;:NE
 6: XT                              
 7: RUN                             
 8:  1  8  27  64  125  216  343  51
 9: 2  729  1000                    
10: OK                              
11: _                               
12:                                 
13:                                 
14:                                 
15:                                 
```

---

## 4. Hardware & Architecture Reference

- **SAM M0 Register (`$FFDB`)**:
  The CoCo 2 motherboard requires `$FFDB` (`M0=1`) to multiplex motherboard DRAM for 16K/64K operation. The console exit routine in `firmware/gspoon.h` preserves this bit to prevent VDG video raster corruption.
- **Data Bus Driving (`/SLENB`)**:
  Centipede drives the data bus and asserts `/SLENB` on read cycles (`R/W = 1`) when serving RAM or injected ROMs. On write cycles (`R/W = 0`), the 6809 MPU drives the bus and writes simultaneously update Centipede's internal memory and motherboard DRAM.
- **Keyboard Scanning**:
  Color BASIC scans the keyboard matrix by writing 8-bit column strobes to PIA0 Port B (`$FF02`) and reading 7-bit row return signals from PIA0 Port A (`$FF00`). The Centipede keyboard injector monitors `$FF02` writes and supplies matching row responses on `$FF00` reads.
