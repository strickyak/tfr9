# rc/mode1.tcl -- executed on restart with Coco on and no keys down.

menu clear
menu fetch Config
menu array Config
menu title "HOME" ;# unused

menu template {
A | ==== Centipede Config ====
B |
C |  [1] Inject 64kB RAM
D | 
E |  [2] Inject Disk Basic Rom "disk11"
F |
G |  [3] On-Board Floppy Disks /fd/f[0123]
H |  [8] Remote   Floppy Disks /pc/f[0123]
I |
J |  [4] Trace Memory Write Cycles
K |  [5] Trace Memory Read Cycles
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
menu check    3 floppy_fd
menu check    8 floppy_pc
menu check    4 trace_writes
menu check    5 trace_reads
menu at-most-one {3 8}

menu action   9 {menu save-and-exit}
menu render
menu store Config
bye
