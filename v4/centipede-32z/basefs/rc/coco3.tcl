# rc/coco3.tcl -- executed on restart in a real CoCo 3 with no keys down.

menu clear
menu fetch Config
menu array Config
menu title "HOME"

menu template {
A | ==== Centipede on a CoCo 3 ====
B |
C |  [2] Disk Basic Rom "disk11"
D |
E |  [3] On-Board Floppy Disks /fd/f[0123]
F |  [8] Remote   Floppy Disks /pc/f[0123]
G |
H |  [4] Trace Memory Write Cycles
I |  [5] Trace Memory Read Cycles
J |
K |  [9] Launch!
L |
M | Use UP/DOWN to navigate.
N | Use SPACE to toggle or execute.
}
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
