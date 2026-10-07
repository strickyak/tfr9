# rc/mode70.tcl -- hold F while restart.
#
# FujiNet: HDB-DOS at $C000, talking to FujiNet over the
# becker port ($FF41/$FF42).  No 64K RAM, no floppy emulation.

menu fetch Config
set Config(ram_64k) 0
set Config(rom_disk11) 0
set Config(floppy_fd) 0
set Config(floppy_pc) 0
set Config(trace_writes) 0
set Config(trace_reads) 0
set Config(fujinet) 1
menu store Config
bye
