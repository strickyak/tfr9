# boot_gep9big.tcl - Hatvan OS (gep9big / EMBIGGEN) for the v5 engine systest
on tfr911h run hatvan for gep9big
set Config(flat_img) "/pc/kernel_6809.decb"
set Config(task1) "/pc/rbf_6809.decb"
set Config(task2) "/pc/procfs_6809.decb"
set Config(drive0) "/pc/hatvan0.dsk"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye
