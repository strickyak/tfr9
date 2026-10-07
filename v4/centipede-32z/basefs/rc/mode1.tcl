# rc/mode1.tcl -- executed on restart with Coco on and no keys down.

menu clear
menu fetch Config
menu array Config
# rom_none is only for the menu: it is ticked when neither ROM is.
set Config(rom_none) [expr {!$Config(rom_disk11) && !$Config(fujinet)}]
menu title "HOME" ;# unused

menu template {
A | ==== Centipede Config ====
B |
C |  [1] Inject 64kB RAM
D |
D |  Rom Injection:
E |    [2] Disk Basic Rom "disk11"
E |    [6] HDBDOS/Fujinet
E |    [7] None
F |
G |  [3] On-Board Floppy Disks /fd/f0-f3
H |  [8] Remote   Floppy Disks /pc/f0-f3
I |
J |  [4] Trace Memory Write Cycles
K |  [5] Trace Memory Read Cycles
K |  [0] Trace Becker Port (FujiNet)
L |
M |  [9] Launch!
N |
O | Designed and tested on a Coco2
P |          with 16kB built-in RAM.
Q |
R | Use UP/DOWN to navigate.
S | Use SPACE to toggle or execute.
}
menu check    1 ram_64k
menu check    2 rom_disk11
menu check    6 fujinet
menu check    7 rom_none
menu check    3 floppy_fd
menu check    8 floppy_pc
menu check    4 trace_writes
menu check    5 trace_reads
menu check    0 trace_becker
menu at-most-one {3 8}
menu at-most-one {2 6 7}

menu action   9 {menu save-and-exit}
menu render
menu store Config
bye
