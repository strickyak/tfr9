#!/bin/sh
# run_centipede32z_coco3_basic.sh - Launch 128KB CoCo 3 Super Extended Color BASIC 2.1 on Centipede 32z ("Become CoCo 3")
set -e
DIR=$(cd "$(dirname "$0")" && pwd)

exec "$DIR/scripts/run-centipede32z-demo.sh" coco3_basic \
  'on coco2 run rsbasic for coco3 with e1 with info with rtc; bye' \
  -- -coco3 "$@"
