# boot_fuzix.tcl -- Fuzix 6809 (engine multicomp09fuzix).  Test fixture staged into the temporary /pc.
set Config(engine) "multicomp09fuzix"
set Config(flat_img) "/pc/fuzix.bin"
set Config(drive0) "/pc/disk.img"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye
