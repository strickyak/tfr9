# boot_fuzix.tcl -- Fuzix 6809 (on tfr911h run fuzix for multicomp09).  Test fixture staged into temporary /pc.
on tfr911h run fuzix for multicomp09
set Config(flat_img) "/pc/fuzix.bin"
set Config(drive0) "/pc/disk.img"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye
