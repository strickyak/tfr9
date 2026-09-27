# rc/mode90.tcl -- hold Z (ASCII 90) while booting to enter CoCo 3 mode.

menu fetch A
set A(become_coco3) 1
set A(ram_64k) 1
set A(rom_disk11) 0
set A(floppy_fd) 0
set A(floppy_pc) 0
set A(trace_writes) 0
set A(trace_reads) 0
menu store A
bye
