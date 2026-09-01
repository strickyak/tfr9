# rc/mode50.tcl -- hold 2 while restart.
#
# Trace writes.

menu fetch Config
set Config(ram_64k) 1
set Config(rom_disk11) 1
set Config(floppy_fd) 0
set Config(floppy_pc) 1
set Config(trace_writes) 1
set Config(trace_reads) 0
menu store Config
bye
