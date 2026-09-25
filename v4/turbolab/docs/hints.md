# HINTS for alpha release of TurboLab subset for TFR/911h

## Flashing the Pico

Restart the Pico with the FLASH button held down.

Look for a mount like `/media/strick/RP2350`

Copy turbolab/build/turbolab_v4.uf2 to the mount.

## Re-flashing the Pico

After the TurboLab firmware is already on the Pico,
instead of pushing the buttons on the Pico,
you can use the command

```
turbolab/build/tether.linux-amd64.exe -reflash
```

to put the Pico into ready-to-be-flashed mode

## Running TurboOS

A simple run.  Where you see [SIGINT] is where the user hits Control-C.

```
$ sh turbolab/run-dev.sh
+ turbolab/build/tether.linux-amd64.exe -listings turbolab/build/listings/ turbolab/build/turbos/turbos_dev.img.rom

[USB Connected: '/dev/ttyACM0'] #%
Shell

TOS:mdir
mdir
 Module directory at 00:00:59
kernel 
init 
tk 
ioman 
scf 
scvt 
term 
go 
shell 
mfree 
mdir 
procs 
sleep 


TOS:
[SIGINT]
```

## Running TurboOS with Trace

Add -trace=1 to turn on lots of tracing, which will appear in the `_log` file.

```
$ sh turbolab/run-dev.sh -trace=1
```

Look at `_log`:

```
Upload complete.
Starting 6309 CPU...
=== TurboLab Running (Type to send, ^C or enter line '^C' to send interrupt) ===
r FFFE D4 #1;s reset vector (high)
r FFFF F2 #2;s reset vector (low)
x D4F2 8E #4; "kernel.0d4eec829c"+0014   ldx #D.FMBM start clearing memory at D.FMBM
+ D4F3 00 #5;
+ D4F4 20 #6;_
x D4F5 10 #7; "kernel.0d4eec829c"+0017   ldy #$400-D.FMBM get the number of bytes to clear
+ D4F6 8E #8;
+ D4F7 03 #9;
+ D4F8 E0 #10;_
x D4F9 4F #11; "kernel.0d4eec829c"+001b   clra clear A
+ D4FA 5F #12;_
x D4FA 5F #13; "kernel.0d4eec829c"+001c   clrb clear B (D now $0000)
+ D4FB ED #14;_
x D4FB ED #15; "kernel.0d4eec829c"+001d loop@ std ,x++ save off at X and increment
+ D4FC 81 #16;
+ D4FD 31 #17;
w 0020 00 #21;
w 0021 00 #22;_
x D4FD 31 #23; "kernel.0d4eec829c"+001f   leay -2,y decrement counter
+ D4FE 3E #24;
+ D4FF 26 #25;
```

## Running with Triggers and Watchpoints (`--trigger` and `--max`)

You can delay tracing until a specific event using `--trigger`, or stop execution automatically with a core dump using `--max`.

### Cycle Limits (`c:`)
Specify cycle counts using decimal SI (`k=1000`, `m=1000000`, `g=1000000000`) or binary multipliers (`K=1024`, `M=1048576`, `G=1073741824`):

```bash
# Run for exactly 50,000 cycles and stop:
turbolab/build/tether.linux-amd64.exe -n --max=c:50k turbolab/build/turbos/turbos_dev.img

# Run for 64K (65,536) cycles:
turbolab/build/tether.linux-amd64.exe -n --max=c:64K turbolab/build/turbos/turbos_dev.img
```

### Module Watchpoints (`@module+offset`)
Target specific OS-9 modules by name without needing hardcoded hex addresses:

* Offset can be decimal or hexadecimal (`$1B`, `0x1B`, `27`).
* Default event type is instruction execution (`x:`):
  * `@kernel+0x1B` is shorthand for `x:@kernel+0x1B:1` (the 1st execution of the instruction at kernel entry).
* Read and write watchpoints:
  * `r:@module+offset[:N]`: Halt on the Nth read.
  * `w:@module+offset[:N]`: Halt on the Nth write.

```bash
# Halt when the CPU executes the first instruction of the OS-9 kernel entry point ($D4F9):
turbolab/build/tether.linux-amd64.exe -n --max=@kernel+0x1B turbolab/build/turbos/turbos_dev.img

# Start tracing only when the CPU reaches the shell module:
turbolab/build/tether.linux-amd64.exe --trigger=@shell turbolab/build/turbos/turbos_dev.img
```

## Running with Basic09

```
$ sh turbolab/run-dev.sh turbolab/data/cmds/basic09
+ turbolab/build/tether.linux-amd64.exe -listings turbolab/build/listings/ turbolab/data/cmds/basic09 turbolab/build/turbos/turbos_dev.img.rom

[USB Connected: '/dev/ttyACM0'] #%
Shell

TOS:basic09
basic09

            BASIC09
     6809 VERSION 01.01.00
COPYRIGHT 1980 BY MOTOROLA INC.
  AND MICROWARE SYSTEMS CORP.
   REPRODUCED UNDER LICENSE
       TO TANDY CORP.
    ALL RIGHTS RESERVED.

Basic09
Ready
B:
[SIGINT]
```

## Running with CLIF (a tiny Command LIne Forth)

```
$ sh turbolab/run-dev.sh turbolab/data/cmds/clif
+ turbolab/build/tether.linux-amd64.exe -listings turbolab/build/listings/ turbolab/data/cmds/clif turbolab/build/turbos/turbos_dev.img.rom

[USB Connected: '/dev/ttyACM0'] #%
Shell

TOS:clif 26 0 DO I 65 + $FF00 CPOKE LOOP
clif 26 0 DO I 65 + $FF00 CPOKE LOOP
ABCDEFGHIJKLMNOPQRSTUVWXYZ
TOS:
[SIGINT]
```

## To send Control-C to TurbOS without killing Tether

Type the two characters on one line: `^` `C` and hit Enter,
to send the control character to TurbOS.

```
$ sh turbolab/run-dev.sh 
+ turbolab/build/tether.linux-amd64.exe -listings turbolab/build/listings/ turbolab/build/turbos/turbos_dev.img.rom

[USB Connected: '/dev/ttyACM0'] #%
Shell

TOS:sleep 999999
sleep 999999
^C
ERROR #003

TOS:
[SIGINT]
```

