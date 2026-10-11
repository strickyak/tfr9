#!/bin/bash
# run_fuzix_gep9.sh - Launch Fuzix 6809 on GEP9 Emulator multicomp09fuzix engine
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

/home/strick/github.com/strickyak/hatvan-os/build/gep9 \
    -engine multicomp09fuzix \
    -ram 256k \
    -disk0 /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/disk.img \
  /home/strick/modoc/coco-shelf/tfr9/v4/misc/pc/fuzix.bin
