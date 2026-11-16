#!/bin/sh
# run_coco2_basic.sh - Launch 64KB CoCo 2 Disk Extended Color BASIC 1.1 on Centipede 32z
set -e
DIR=$(cd "$(dirname "$0")" && pwd)

exec "$DIR/scripts/run-centipede-demo.sh" coco2_basic \
  'on coco2 run rsbasic for coco2 with e1 with info with rtc; bye' \
  -- "$@"
