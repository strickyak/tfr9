# boot_flat.tcl -- NitrOS-9 Level 1 (engine flat65280).  Test fixture staged into the temporary /pc.
set Config(engine) "flat65280"
set Config(flat_img) "/pc/flat65280.img"
set Config(drive0) "/pc/flat0.dsk"
set Config(drive1) ""
menu store Config
bye
