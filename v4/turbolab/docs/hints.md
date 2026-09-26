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

Another good option is `--trace=i` (that's a little letter `i`).
This option logs only interrupts (both hardware and software) and RTIs.

When OS-9 SWI2 kernel traps occur, they are nicely decoded for you:

```
$
$ grep 'F[$]' _log | head
i SWI2 _1_ #70346 at $D591: $00 = F$Link ( A=lang_and_type=$C0, X=$module_name_ptr=$E219="Init" )
i RTI  _1_ #71394 returning to SWI2 $00 (F$Link): ( RA=lang_and_type=$C0, RB=attr_and_rev=$80, RX=$after_module_name=$E21D, RY=absolute_entry_addr=$E32A, RU=absolute_header_addr=$E22C )
i SWI2 _2_ #71448 at $D5AB: $00 = F$Link ( A=lang_and_type=$C0, X=$module_name_ptr=$E24F="tk" )
i RTI  _2_ #72429 returning to SWI2 $00 (F$Link): ( RA=lang_and_type=$C1, RB=attr_and_rev=$81, RX=$after_module_name=$E251, RY=absolute_entry_addr=$E28F, RU=absolute_header_addr=$E262 )
i SWI2 _3_ #72497 at $E2A7: $32 = F$SSvc ( Y=init_table_addr=$E272 )
i RTI  _3_ #72884 returning to SWI2 $32 (F$SSvc): ( ok )
i SWI2 _4_ #73324 at $D5C4: $30 = F$All64 ( X=base_addr=$0000 )
i SWI2 _5_ #73457 at $DFDF: $28 = F$SRqMem ( D=byte_count=$0100 )
i RTI  _5_ #75408 returning to SWI2 $28 (F$SRqMem): ( RD=actual_size=$0100, RU=starting_addr=$D300 )
i RTI  _4_ #79720 returning to SWI2 $30 (F$All64): ( RA=block_number=$01, RX=base_addr_out=$D300, RY=address_of_block=$D340 )
$
$ grep 'I[$]' _log | head
i SWI2 _6_ #79805 at $D608: $86 = I$ChgDir ( A=mode=$05, X=$pathname=$E253="/dd" )
i SWI2 _15_ #104298 at $E778: $80 = I$Attach ( A=access_mode=$85, X=$pathname=$E254="dd" )
i SWI2 _17_ #109487 at $E537: $81 = I$Detach ( U=device_table_entry_addr=$04C3 )
i RTI  _17_ #110396 returning to SWI2 $81 (I$Detach): ( ok )
i RTI  _15_ #110472 returning to SWI2 $80 (I$Attach): ERROR $DD (221.) E$MNF: Module Not Found
i RTI  _6_ #110857 returning to SWI2 $86 (I$ChgDir): ERROR $DD (221.) E$MNF: Module Not Found
i SWI2 _23_ #115890 at $D608: $86 = I$ChgDir ( A=mode=$05, X=$pathname=$E253="/dd" )
i SWI2 _26_ #117586 at $E778: $80 = I$Attach ( A=access_mode=$85, X=$pathname=$E254="dd" )
i SWI2 _28_ #122775 at $E537: $81 = I$Detach ( U=device_table_entry_addr=$04C3 )
i RTI  _28_ #123684 returning to SWI2 $81 (I$Detach): ( ok )
$
```

The labels like `_6_` are paired calls and returns (or so our heuristics think).

## Running with Triggers and Watchpoints (`--trigger` and `--stop`)

You can delay tracing until a specific event using `--trigger`, or stop execution automatically with a core dump using `--stop`.

### Cycle Limits (`c:`)
Specify cycle counts using decimal SI (`k=1000`, `m=1000000`, `g=1000000000`) or binary multipliers (`K=1024`, `M=1048576`, `G=1073741824`):

```bash
# Run for exactly 50,000 cycles and stop:
turbolab/build/tether.linux-amd64.exe -n --stop=c:50k turbolab/build/turbos/turbos_dev.img

# Run for 64K (65,536) cycles:
turbolab/build/tether.linux-amd64.exe -n --stop=c:64K turbolab/build/turbos/turbos_dev.img
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
turbolab/build/tether.linux-amd64.exe -n --stop=@kernel+0x1B turbolab/build/turbos/turbos_dev.img

# Start tracing only when the CPU reaches the shell module:
turbolab/build/tether.linux-amd64.exe --trigger=@shell turbolab/build/turbos/turbos_dev.img
```

### Logging Watchpoints (`--watch=addr1,addr2,...`)

Log individual matching bus cycles to stderr, even when general tracing (`--trace`) is disabled, or before the trigger cycle is reached:

* Target addresses can be numeric literals (`0x0020`, `$0020`, `32`) or symbolic OS-9 module offsets (`@kernel+0x1B`).
* Supports cycle kind filters:
  * `0x0020` or `*:0x0020`: Log all cycles accessing `$0020` (reads, writes, and instruction fetches).
  * `r:0x0020`: Log only read cycles.
  * `w:0x0020`: Log only write cycles.
  * `x:0x0020`: Log only instruction fetch (FIC) cycles.
* Supports optional Nth count trigger (`addr:N`):
  * `0x1019:3`: Log only the 3rd access to `$1019`.
* Can be combined with `--trace`, `--trigger`, and `--stop`.

```bash
# Log every access to zero-page address $0020 without tracing other instructions:
turbolab/build/tether.linux-amd64.exe -n --watch=0x0020 --stop=c:50k turbolab/build/turbos/turbos_dev.img

# Watch writes to $0020, and trace instructions once @kernel+0x1B is reached:
turbolab/build/tether.linux-amd64.exe --watch=w:0x0020 --trigger=@kernel+0x1B --trace=x turbolab/build/turbos/turbos_dev.img
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

