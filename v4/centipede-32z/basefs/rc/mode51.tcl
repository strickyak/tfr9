# rc/mode51.tcl -- hold 3 while restart.
#
# Trace writes and reads.
# Danger: runs slowly, logs lots, prone to crash.

menu fetch Config
set Config(ram_64k) 1
set Config(rom_disk11) 1
set Config(floppy_fd) 1
set Config(floppy_pc) 0
set Config(trace_writes) 1
set Config(trace_reads) 1
menu store Config
bye
