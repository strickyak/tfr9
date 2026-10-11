# boot_flat.tcl - Launch flat65280 engine (NitrOS-9 Level 1) on TFR911 firmware
# Usage from Tcl prompt:
#   source /pc/boot_flat.tcl

set Config(engine) "flat65280"
set Config(flat_img) "/pc/flat65280.img" ;# 64KB NitrOS-9 Level 1 boot image
set Config(drive0) "/pc/flat0.dsk"
set Config(drive1) ""
menu store Config
bye
