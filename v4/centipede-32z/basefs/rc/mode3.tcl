# rc/mode3.tcl -- become coco3 mode

menu fetch Config
set Config(become_coco3) 1
set Config(ram_64k) 1
set Config(rom_disk11) 0
set Config(floppy_fd) 0
set Config(floppy_pc) 0
set Config(trace_writes) 0
set Config(trace_reads) 0
menu store Config
bye
