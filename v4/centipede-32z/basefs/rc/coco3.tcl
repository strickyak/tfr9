# rc/coco3.tcl -- executed on restart in a real CoCo 3 with no keys down.

menu clear
menu fetch Config
menu array Config
menu title "HOME"

menu template {
A | ==== Centipede on a CoCo 3 ====
B |
C |  [4] Trace Memory Write Cycles
D |  [5] Trace Memory Read Cycles
E |
F |  [9] Launch!
G |
H | RAM, ROM and floppy emulation
I | are not available on a CoCo 3.
J |
K | Use UP/DOWN to navigate.
L | Use SPACE to toggle or execute.
}
menu check    4 trace_writes
menu check    5 trace_reads

menu action   9 {menu save-and-exit}
menu render
menu store Config
bye
