# Quick Notes for Early Testers

```
$ cp tfr9-release--linux-x86-64--2024-01-30-NitrOS9-Level2.zip /tmp/

$ cd /tmp/

/tmp$ unzip tfr9-release--linux-x86-64--2024-01-30-NitrOS9-Level2.zip
Archive:  tfr9-release--linux-x86-64--2024-01-30-NitrOS9-Level2.zip
  inflating: 2024-01-30-NitrOS9-Level2/level2.disk
  inflating: 2024-01-30-NitrOS9-Level2/tconsole
  inflating: 2024-01-30-NitrOS9-Level2/tmanager.uf2

/tmp$ cd 2024-01-30-NitrOS9-Level2/

/tmp/2024-01-30-NitrOS9-Level2$ ll
total 5828
-rw-rw-r-- 1 strick strick 2559744 Jan 30 00:11 level2.disk
-rwxrwxr-x 1 strick strick 2829793 Jan 30 00:11 tconsole*
-rw-rw-r-- 1 strick strick  576512 Jan 30 00:12 tmanager.uf2

[ plug in the Pico W (the one with the silver box) USB cable
  to your Linux box while holding down the little button
  on that Pico W, so a directory like /media/strick/RPI-RP2/
  will appear.  Fix the exact path.  ]

/tmp/2024-01-30-NitrOS9-Level2$ cp -v tmanager.uf2 /media/strick/RPI-RP2/
'tmanager.uf2' -> '/media/strick/RPI-RP2/tmanager.uf2'

[ unplug the Pico W, and then plug it in after you start the
  next command.   It will print PANIC messages once a second
  until the device is recognized. ]

strick@nand:/tmp/2024-01-30-NitrOS9-Level2$ ./tconsole --tty /dev/ttyACM0 --disks level2.disk 2>log
[recover: "PANIC: serial.Open: open /dev/ttyACM0: no such file or directory\n"]
[recover: "PANIC: serial.Open: open /dev/ttyACM0: no such file or directory\n"]
[recover: "PANIC: serial.Open: open /dev/ttyACM0: no such file or directory\n"]
+3+ +2+ +1+ +XYZ+
NitrOS-9/6809 Level 2
TFR/901
(C) 2014 The NitrOS-9 Project
**   DEVELOPMENT BUILD   **
** NOT FOR DISTRIBUTION! **
Tue Nov  5 19:24:51 2024
http://www.nitros9.org

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
B:{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.}[0.722885 : 1 :  0.722885]
{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.}[0.722368 : 2 :  0.722626]
{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.}[0.722746 : 3 :  0.722666]
{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.}[0.722990 : 4 :  0.722747]
{*1.*2.*3.*4.*5.*6.*7.*8.*9.*10.*11.*12.*13.*14.*15.*16.*17.*18.*19.*20.*21.*22.*23.*24.*25.*26.*27.*28.*29.*30.*31.*32.*33.*34.*35.*36.*37.*38.*39.*40.*41.*42.*43.*44.*45.*46.*47.*48.*49.*50.*51.*52.*53.*54.*55.*56.*57.*58.*59.*60.*61.*62.*63.*64.*65.*66.*67.*68.*69.*70.*71.*72.*73.*74.*75.*76.*77.*78.*79.*80.*81.*82.*83.*84.*85.*86.*87.*88.*89.*90.*91.*92.*93.*94.*95.*96.*97.*98.*99.}[0.722315 : 5 :  0.722661]
Ready
B:
Shell

OS9:dir
dir

 Directory of .  2024/12/25 12:15
CMDS            OS9Boot         startup

OS9:copy startup z1
copy startup z1

OS9:list z1
list z1
basic09
e
100 for j=1 to 5
210 print "{";
220 FOR z=1 to 99
240 print "*";z;
250 next z
290 print "}[0.032997 : 6 :  0.607717]"
810 FOR z=0 to 300
890 next z
900 next j
q
run

OS9:mdir
mdir

   Module Directory at 12:15:51
REL         Boot        Krn         Shell       Echo        MDir
Dir         SMap        MMap        PMap        Term        sc6850
Init        EmuDsk      DD          Clock       Clock2      SysGo
RBF         SCF         IOMan       KrnP2

OS9:mdir -e
mdir -e

   Module Directory at 12:15:53

Block Offset Size Typ Rev Attr  Use Module Name
----- ------ ---- --- --- ---- ---- ------------
  3F    D06   12A  C1   6 r...    0 REL
  3F    E30   1D0  C1   0 r...    1 Boot
  3F   1000   EDF  C0   0 r...    0 Krn
   2   1F00   602  11   0 r...    3 Shell
   2   2502    22  11   1 r...    0 Echo
   2   2524   2ED  11   1 r...    2 MDir
   2   2811   3A1  11   0 r...    1 Dir
   2   2BB2   1CF  11   0 r...    0 SMap
   2   2D81   1E9  11   0 r...    0 MMap
   2   2F6A   1F2  11   0 r...    0 PMap
   2   315C    3F  F1   0 r...    2 Term
   2   319B   405  E1   0 r...    2 sc6850
   2   35A0    61  C0   0 r...    2 Init
   2   3601    EC  E1   2 r...   12 EmuDsk
   2   36ED    2F  F1   1 r...   12 DD
   2   371C   207  C1   5 r...    1 Clock
   2   3923    76  21   0 r...    1 Clock2
   2   3999   1FC  11   3 r...    1 SysGo
   2   3B95  12EE  D1   3 r...   12 RBF
   2   4E83   778  D1   0 r...    2 SCF
   2   55FB   A25  C1   6 r...    1 IOMan
   2   6020   CDB  C0   0 r...    1 KrnP2

OS9:smap
smap

    0 1 2 3 4 5 6 7 8 9 A B C D E F
 #  = = = = = = = = = = = = = = = =
 0  U U U U U U U U U U U U U U U U
 1  U U U U U U U U U U U U U U U U
 2  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 3  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 4  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 5  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 6  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 7  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 8  _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 9  _ _ _ U U U U U U U U U U U U U
 A  U U U U U U U U U U U U U U U U
 B  U U U U U U U U U U U U U U U U
 C  U U U U U U U U U U U U U U U U
 D  U U U U U U U U U U U U U U U U
 E  U U U U U U U U U U U U U U U U
 F  U U U U U U U U U U U U U U U .

 Number of Free Pages: 115
   RAM Free in KBytes:  28

OS9:pmap
pmap

 ID   01 23 45 67 89 AB CD EF  Program
____  __ __ __ __ __ __ __ __  ___________
  1   00 .. .. 01 02 03 04 3F  SYSTEM
  2   07 .. .. .. 02 03 04 3F  Shell
  3   05 .. .. .. 02 03 04 3F  PMap

OS9:mmap
mmap

     0 1 2 3 4 5 6 7 8 9 A B C D E F
  #  = = = = = = = = = = = = = = = =
 00  U U U U U U _ U _ _ _ _ _ _ _ .
  Block Size: 8192
 Free Blocks: 8
 KBytes Free: 64

OS9:free
free

"TFR9-DISK" created on: 2025/01/28
Capacity: 9,999 sectors (1-sector clusters)
8,977 free sectors, largest block 8,977 sectors

OS9:disk free
disk free
ERROR #216

OS9:mfree
mfree

 Blk Begin   End   Blks  Size
 --- ------ ------ ---- ------
   8  10000  1DFFF    7    56k
                   ==== ======
            Total:    7    56k

OS9:

OS9:

OS9:

OS9:^C
```

Note that tconsole does special handling with { and } characters.
{ remembers the realtime, and } causes it to print the time
differnce, plus the count and the average of such time measurements.

It might be very confusing when you see these timings,
as they are not really there on the NitrOS9 machine.
