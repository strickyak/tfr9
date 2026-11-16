# boot_deep.tcl -- NitrOS-9 Level 2 (on tfr911h run nitros9l2 for deep65280).  Test fixture staged into temporary /pc.
on tfr911h run nitros9l2 for deep65280
set Config(flat_img) "/pc/deep65280.img"
set Config(drive0) "/pc/deep0.dsk"
set Config(drive1) ""
menu store Config
bye
