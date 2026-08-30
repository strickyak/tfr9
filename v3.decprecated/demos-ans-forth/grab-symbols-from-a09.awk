$3 == "address" && $6 == "forth__vector_bye" { print "-DFORTH_ORIGIN=0x" $4 }
$3 == "address" && $6 == "forth__ds_top" { print "-DSYM_DS_TOP=0x" $4 }
$3 == "address" && $6 == "forth__rs_top" { print "-DSYM_RS_TOP=0x" $4 }
$3 == "address" && $6 == "forth__source" { print "-DSYM_SOURCE=0x" $4 }
$3 == "address" && $6 == "forth_core_quit.xt" { print "-DSYM_QUIT=0x" $4 }
$3 == "address" && $6 == "forth_core_execute.asm" { print "-DSYM_EXECUTE=0x" $4 }
$3 == "equate" && $6 == "INPUT_SIZE" { print "-DSYM_INPUT_SIZE=0x" $4 }
