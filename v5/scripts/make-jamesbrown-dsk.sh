#!/bin/sh
# make-jamesbrown-dsk.sh -- build build/tfr911-jamesbrown.dsk for run_tfr911h_jamesbrown.sh.
#
# Starts from Andrew Diller's nitros9-l2 disk image (building it first if needed),
# stamps the OS-9 RBF LSN 0 volume name to 'tfr911-jamesbrown.dsk', and installs
# os/nitros9/cmds/basic09 (imported from v4/turbolab/data/cmds/basic09) into CMDS
# with attributes r e pr pe.
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"

L2_DIR=${DILLERA_L2_DIR:-$HOME/github.com/dillera/tfr9/v4/nitros9-l2}
BASE_DSK=${JAMESBROWN_BASE_DSK:-$L2_DIR/build/tfr911-level2.dsk}
BASIC09_MOD="$V5/os/nitros9/cmds/basic09"
OUT=${1:-$V5/build/tfr911-jamesbrown.dsk}

if [ ! -f "$BASIC09_MOD" ]; then
  echo "error: missing $BASIC09_MOD" >&2
  exit 1
fi

if [ ! -f "$BASE_DSK" ]; then
  if [ ! -d "$L2_DIR" ]; then
    echo "error: $BASE_DSK not found and $L2_DIR does not exist" >&2
    exit 1
  fi
  mkdir -p "$L2_DIR/build"
  if [ ! -f "$L2_DIR/build/helpmsg" ] && [ -f "$L2_DIR/helpmsg.unix" ]; then
    cp "$L2_DIR/helpmsg.unix" "$L2_DIR/build/helpmsg"
  fi
  make -C "$L2_DIR" NITROS9_DIR="$SHELF/nitros9" FN_APPS="" build/tfr911-level2.dsk
fi

mkdir -p "$(dirname "$OUT")"
cp "$BASE_DSK" "$OUT"

# Stamp RBF LSN 0 DD.NAM (offset 0x1F, 32 bytes, high-bit terminated) to 'tfr911-jamesbrown.dsk'.
python3 - "$OUT" << 'PY'
import sys
path = sys.argv[1]
with open(path, "r+b") as f:
    name = bytearray(b"tfr911-jamesbrown.dsk")
    name[-1] |= 0x80
    field = bytes(name) + b"\x00" * (32 - len(name))
    assert len(field) == 32
    f.seek(0x1F)
    f.write(field)
PY

os9 copy -r "$BASIC09_MOD" "$OUT,CMDS/basic09"
os9 attr -q -r -e -pr -pe -nw -npw -ns "$OUT,CMDS/basic09"

# Build and install the 5 CMOC C FujiNet apps (fnnews, fnweather, fnstocks, fnbounce, fnbattleship)
FN_APPS_DIR="$V5/build/fn-apps"
sh "$V5/scripts/build-jamesbrown-fn-apps.sh" "$FN_APPS_DIR"
for app in fnnews fnweather fnstocks fnbounce fnbattleship; do
  os9 copy -r "$FN_APPS_DIR/bin/$app" "$OUT,CMDS/$app"
  os9 attr -q -r -w -e -pr -pe "$OUT,CMDS/$app"
done

echo "built $OUT (with CMDS/basic09 + fnnews fnweather fnstocks fnbounce fnbattleship)"
