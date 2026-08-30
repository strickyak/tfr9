# README for tfr905-runtime-bundle-2025-07-14...

This is the runtime-release for the TFR/905h board.  It contains enough
files for you to use these three operating systems on your TFR/905h:

*  0: TurbOS
*  1: NitrOS-9 Level 1
*  2: NitrOS-9 Level 2

A source release (SDK) should be available soon.

Plus, the githubs will be updated.

## Quick Start (Ubuntu/Debian)

Unzip the bundle and chdir into it.

Hold down the little white button (by the USB connector) on the Pi Pico
sub-board while you plug in the USB cable between it and your PC.

Type this command:

`$ make flash run`

After the firmware is copied to the Pi Pico, you'll probably see a couple
of "recover: PANIC" error messages.  Ignore them.  That's the tconsole
program waiting for the USB device to show up and be configured.

Then you should see an chain of characters like this:

`;.:,;.:,;.:,;.:,;.:,`

Those characters are the "synchronization sequence" that means the TFR9 is
ready to launch an OS.  Don't wait for them to stop.  They won't stop.
Also, the LED on the Pi Pico should do two quick flashes every two
seconds.

Now type just one digit `0`, `1`, or `2` for the
operating system you want to boot (see OS list above),
and it should boot.

If it is "0" TurbOS, it boots very quickly and gives you a
shell prompt.  Only 4 commands are available in
this little demo: mdir, procs, mfree, and shell.
This version of TurbOS has no hard drive.

If it is "1" Level 1 or "2" Level 2, it will run the
STARTUP file which has it execute a benchmark program
in Basic09, and then give you a shell prompt.  

In Level 1, the default disk device `/DD` is the same
as virtual hard drive `/H1`, and maps to the Linux
file `generated/level1.dsk`.

In Level 2, the default disk device `/DD` is the same
as virtual hard drive `/H2`, and maps to the Linux
file `generated/level2.dsk`.

All drives `/H0` to `/H7` are the same for Level 1
and Level 2, and are in the `generated` directory.

Hit Control-C (or whatever the Keyboard Interrupt 
characters is on your PC) to kill the session.

If you are not going to re-flash the firmware into
the Pico, you can just type

`make run`

to start the session.  Hint: Plug in the USB cable AFTER you start the
session. Hint: Start a new session on your PC every time you restart
(i.e. re-plug-in) the TFR9.

## Compatibility

It works with either a Pico1 or a Pico2 on the board, but the Pico2 code
has been optimized for speed and is capable of overclocking to 270MH.
The Pico1 has not been optimized and will be maybe half the speed,
and cannot be overclocked.

This is known to work on Ubuntu/Debian style 64-bit AMD/Intel
Linux.  The makefile is hardwired to use the version of
tconsole for that kind of machine;  edit Makefile and change `run:
... generated/tconsole.linux-amd64.exe` to match your platform.  The least
likely to work are the Windows (*win*) versions.

If the call to `stty` (from within tconsole) doesn't work on your machine,
create a dummy version of `stty` that does nothing, and put it up front
in your $PATH.    It could be a shell script that just calls `true`.
Then you will have to hit ENTER after typing `0`, `1`, or `2`
(because your tty will be cooked).

## If something goes wrong

Look in the file name `_log` that gets created.
There may be an important error message at the beginning
or at the end of it.

You can delete `_log` if you want.
But it will come back, unless you change the stderr routing
for `make run`.

## Advanced Debugging

When choosing your OS, if you add 5 to the digit,
you get a debugging version of the firmware:

*  5: TurbOS
*  6: NitrOS-9 Level 1
*  7: NitrOS-9 Level 2

It will run slower, and product more debugging
output to the `_log` file.

For a cycle-by-cycle trace of execution,
add the letter `e` before the number:

*  e5: TurbOS
*  e6: NitrOS-9 Level 1
*  e7: NitrOS-9 Level 2

That produces an enormous amount of `_log` output.

Unfortunately, the feature to join source code lines
to that cycle output is not working.  It'll be fixed
in the next release.

## Overclocking

If you have a Pico2 on your TFR/905h, you can try
overclocking it by hitting `v` `c` `x` or `z` before
the digits `0` `1` or `2`.

* v: 200 MHz
* c: 250 MHz
* x: 260 MHz
* z: 270 MHz

Otherwise, the standard clock rate is 150 MHz.

I've tried three different TFR/905h boards, and all
of them overclock to 270 MHz.  (With some work,
they may be able to go faster still.)

## Sample Run 

This shows unbundling the software and running it,
hitting "1" during the synchronization sequence,
to run NitrOS-9 Level 1.

```
strick@nand:/tmp$
strick@nand:/tmp$ ll *.zip
-rw-rw-r-- 1 strick strick 15253509 Jul 14 22:24 tfr9-runtime-bundle-2025-07-14-22-23.zip
strick@nand:/tmp$ unzip tfr9-runtime-bundle-2025-07-14-22-23.zip
Archive:  tfr9-runtime-bundle-2025-07-14-22-23.zip
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/Makefile
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/README.runtime-bundle-2025-07-14.md
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/create-nitros9disk.sh
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/disk0
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/disk3
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/disk4
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/disk5
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/disk6
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/disk7
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/level1.dsk
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/level1.rom.h
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/level2.dsk
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/level2.rom.h
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.linux-386.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.linux-amd64.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.linux-arm-7.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.linux-arm64.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.mac-amd64.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.mac-arm64.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.win-386.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tconsole.win-amd64.exe
  inflating: tfr9-runtime-bundle-2025-07-14-22-23/generated/tmanager.pico1_or_pico2.uf2
strick@nand:/tmp$ cd tfr9-runtime-bundle-2025-07-14-22-23/
strick@nand:/tmp/tfr9-runtime-bundle-2025-07-14-22-23$
strick@nand:/tmp/tfr9-runtime-bundle-2025-07-14-22-23$ make flash run
cp -vf generated/tmanager.pico1_or_pico2.uf2 /media/$USER/RP*/.
'generated/tmanager.pico1_or_pico2.uf2' -> '/media/strick/RP2350/./tmanager.pico1_or_pico2.uf2'
generated/tconsole.linux-amd64.exe  -disks 'generated/disk0,generated/level1.dsk,generated/level2.dsk,generated/disk3,generated/disk4,generated/disk5,generated/disk6,generated/disk7'  -borges /tmp/borges  2>_log
[recover: "PANIC: serial.Open: open /dev/ttyACM0: no such file or directory"]
;.:,;.:,;.:,;.:,;.:,;.:,;

[[ HERE is where I hit "1" ]]

IN ApqrsBstCDE
RC LZ PIO APID ART RMC PRE FFFF LOOP
========
~~~~~~~~
NitrOS-9/6809 Level 1 VI.7.>
Radio Shack Color Computer
(C) 2014 The NitrOS-9 Project
Mon Jul 14 11:59:24 2025
http://www.nitros9.org
basic09
{12}            BASIC09
     6809 VERSION 01.01.00
COPYRIGHT 1980 BY MOTOROLA INC.
  AND MICROWARE SYSTEMS CORP.
   REPRODUCED UNDER LICENSE
       TO TANDY CORP.
    ALL RIGHTS RESERVED.
Basic09
Ready
B:PROCEDURE Program
*
E:*
E:*
E:*
E:*
E:*
E:*
E:*
E:*
E:*
E:Ready
B:^{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.^}[0.786202 : 1 :  0.786202]
^{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.^}[0.780433 : 2 :  0.783317]
^{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.^}[0.784654 : 3 :  0.783763]
^{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.^}[0.783760 : 4 :  0.783762]
^{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.^}[0.783344 : 5 :  0.783679]
Ready
B:
Shell
OS9:mdir -e
  Module directory at 00:00:07
Addr Size Typ Rev Attr Use Module name
---- ---- --- --- ---- --- ------------
EE06   75  C1   6 r...   0 REL
EE7B  7F4  C1   0 r...   0 Krn
F67B  510  C1   0 r...   1 KrnP2
FB8B   76  C0   0 r...   2 Init
FC01  1D0  C1   0 r...   1 Boot
BE00  70A  C1   0 r...   1 IOMan
C50A  17E  C1   8 r...   1 Clock
C688   66  21   0 r...   1 Clock2
C6EE  6D0  D1   0 r...   2 SCF
CDBE  407  E1   0 r...   2 sc6850
D1C5   3F  F1   0 r...   2 Term
D204  DE5  D1   0 r...   0 RBF
DFE9   F0  E1   2 r...   0 EmuDsk
E0D9   2F  F1   1 r...   0 DD
E108   2F  F1   1 r...   0 H0
E137   2F  F1   1 r...   0 H1
E166   2F  F1   1 r...   0 H2
E195   2F  F1   1 r...   0 H3
E1C4   2F  F1   1 r...   0 H4
E1F3   2F  F1   1 r...   0 H5
E222   2F  F1   1 r...   0 H6
E251   2F  F1   1 r...   0 H7
E280  23E  D1   1 r...   0 PipeMan
E4BE   28  E1   0 r...   0 Piper
E4E6   26  F1   0 r...   0 Pipe
E50C  1A9  11   3 r...   0 SysGo
E6B5  627  11   0 r...   1 Shell
B200  238  11   0 r...   1 mdir
OS9:cputype
CPU: 6309, running in 6809 mode.
OS9:dir cmds
 Directory of cmds  1986/01/01 00:00
asm             attr            backup          bawk            binex
build           calldbg         cmp             cobbler         copy
cputype         date            dcheck          debug           ded
deiniz          del             deldir          devs            dir
dirsort         disasm          display         dmode           dsave
dump            echo            edit            error           exbin
format          free            grep            grfdrv          help
ident           iniz            irqs            link            list
load            login           makdir          mdir            megaread
merge           mfree           minted          more            mpi
os9gen          padrom          park            printerr        procs
prompt          rename          save            setime          shell_21
shellplus       sleep           tee             touch           tsmon
tuneport        unlink          verify          xmode           tmode
basic09         runb            gfx             inkey           syscall
dw              inetd           telnet          httpd           tfr9cmd
tfr9log
OS9:
OS9:
OS9:make: *** [Makefile:16: run] Interrupt
```
