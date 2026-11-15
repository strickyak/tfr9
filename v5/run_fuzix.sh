#!/bin/sh
# run_fuzix.sh - Launch Fuzix 6809 (multicomp09) on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
BIN=${V5_IMG_FUZIX_BIN:-$HOME/nand/github.com/EtchedPixels/FUZIX/Kernel/fuzix.bin}
DISK=${V5_IMG_FUZIX_DISK:-$HOME/nand/github.com/EtchedPixels/FUZIX/Images/multicomp09/disk.img}

exec "$DIR/scripts/run-demo.sh" fuzix 1170 \
  'on tfr9 run fuzix for multicomp09
set Config(flat_img) "/pc/fuzix.bin"
set Config(drive0) "/pc/disk.img"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye' \
  "$BIN:fuzix.bin" "$DISK:disk.img" -- "$@"
