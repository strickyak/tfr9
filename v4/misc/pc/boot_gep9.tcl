# boot_gep9.tcl - Launch gep9 (Hatvan 6309) engine on TFR911 firmware
# Usage from Tcl prompt:
#   source /pc/boot_gep9.tcl

set Config(engine) "gep9"
set Config(flat_img) "/pc/kernel_6809.decb"
set Config(task1) "/pc/rbf_6809.decb"
set Config(task2) "/pc/procfs_6809.decb"
set Config(drive0) "/pc/hatvan0.dsk"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye
