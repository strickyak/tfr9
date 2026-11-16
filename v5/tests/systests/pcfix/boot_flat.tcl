# boot_flat.tcl -- NitrOS-9 Level 1 (on tfr911h run nitros9l1 for flat65280).  Test fixture staged into temporary /pc.
on tfr911h run nitros9l1 for flat65280
set Config(flat_img) "/pc/flat65280.img"
set Config(drive0) "/pc/flat0.dsk"
set Config(drive1) ""
menu store Config
bye
