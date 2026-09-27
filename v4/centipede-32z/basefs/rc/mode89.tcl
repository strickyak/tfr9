# rc/mode89.tcl -- hold Y (ASCII 89) while booting to enter CoCo 3 mode with trace_writes.

menu fetch Config
set Config(become_coco3) 1
set Config(ram_64k) 1
set Config(rom_disk11) 1
set Config(floppy_fd) 1
set Config(floppy_pc) 0
set Config(trace_writes) 1
set Config(trace_reads) 0
menu store Config
bye
