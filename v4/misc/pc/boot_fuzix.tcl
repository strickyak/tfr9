# boot_fuzix.tcl - Launch Fuzix 6809 on TFR911H SBC (Hitachi 6309E CPU) with multicomp09fuzix engine
# Usage from Tcl prompt:
#   source /pc/boot_fuzix.tcl

set Config(engine) "multicomp09fuzix"
set Config(flat_img) "/pc/fuzix.bin"
set Config(drive0) "/pc/disk.img"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye
