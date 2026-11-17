#!/bin/sh
# run_tfr911h_fuzix.sh - Launch Fuzix 6809 (multicomp09) on TFR911H SBC (Hitachi 6309E CPU)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
if [ -n "${V5_IMG_FUZIX_BIN:-}" ]; then
  BIN=$V5_IMG_FUZIX_BIN
elif [ -f "$DIR/images/fuzix/fuzix.bin" ]; then
  BIN="$DIR/images/fuzix/fuzix.bin"
else
  BIN="$HOME/nand/github.com/EtchedPixels/FUZIX/Kernel/fuzix.bin"
fi

if [ -n "${V5_IMG_FUZIX_DISK:-}" ]; then
  DISK=$V5_IMG_FUZIX_DISK
elif [ -f "$DIR/images/fuzix/disk.img" ]; then
  DISK="$DIR/images/fuzix/disk.img"
else
  DISK="$HOME/nand/github.com/EtchedPixels/FUZIX/Images/multicomp09/disk.img"
fi

exec "$DIR/scripts/run-tfr911h-demo.sh" fuzix 9050 \
  'on tfr911h run fuzix for multicomp09
set Config(flat_img) "/pc/fuzix.bin"
set Config(drive0) "/pc/disk.img"
set Config(drive1) ""
set Config(drive2) ""
set Config(drive3) ""
menu store Config
bye' \
  "$BIN:fuzix.bin" "$DISK:disk.img" -- "$@"
