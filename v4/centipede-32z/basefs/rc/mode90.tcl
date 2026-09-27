# rc/mode90.tcl -- hold Z (ASCII 90) while booting to enter CoCo 3 mode with trace_writes and trace_reads.

menu fetch Config
set Config(become_coco3) 1
set Config(ram_64k) 1
set Config(rom_disk11) 1
set Config(floppy_fd) 1
set Config(floppy_pc) 0
set Config(trace_writes) 1
set Config(trace_reads) 1
menu store Config
bye
