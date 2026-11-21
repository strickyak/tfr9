#!/bin/sh
# run-gep9-jamesbrown.sh - Simulate NitrOS-9 Level 2 (jamesbrown) on gep9 (--jamesbrown)
# with foggynet (simulated ESP32 FujiNet inside tether making live TCP/HTTP/HTTPS/TNFS connections).
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

if [ -n "${V5_IMG_JAMESBROWN_DISK:-}" ]; then
  SRC_DISK=$V5_IMG_JAMESBROWN_DISK
elif [ -f "$DIR/images/jamesbrown/tfr911-jamesbrown.dsk" ]; then
  SRC_DISK="$DIR/images/jamesbrown/tfr911-jamesbrown.dsk"
else
  SRC_DISK="$DIR/build/tfr911-jamesbrown.dsk"
  if [ ! -f "$SRC_DISK" ] && [ -f "$DIR/scripts/make-jamesbrown-dsk.sh" ]; then
    sh "$DIR/scripts/make-jamesbrown-dsk.sh" "$SRC_DISK"
  fi
fi

if [ ! -f "$SRC_DISK" ]; then
  echo "error: missing jamesbrown disk image: $SRC_DISK" >&2
  exit 1
fi

# Locate or build gep9 (--jamesbrown)
GEP9_BIN=${GEP9:-}
if [ -z "$GEP9_BIN" ]; then
  if [ -x "$DIR/build/gep9" ]; then
    GEP9_BIN="$DIR/build/gep9"
  elif [ -d "/home/strick/github.com/strickyak/hatvan-os/cmd/gep9" ]; then
    mkdir -p "$DIR/build"
    (cd "/home/strick/github.com/strickyak/hatvan-os" && go build -o "$DIR/build/gep9" ./cmd/gep9)
    GEP9_BIN="$DIR/build/gep9"
  elif command -v gep9 >/dev/null 2>&1; then
    GEP9_BIN=$(command -v gep9)
  else
    echo "error: gep9 not found (set GEP9=/path/to/gep9 or build hatvan-os/cmd/gep9)" >&2
    exit 1
  fi
fi

PC_DIR="$DIR/build/demo-pc/jamesbrown-gep9"
mkdir -p "$PC_DIR"
WORK_DISK="$PC_DIR/tfr911-jamesbrown.dsk"

if [ ! -f "$WORK_DISK" ] || [ -n "$(find "$SRC_DISK" -newer "$WORK_DISK" 2>/dev/null)" ]; then
  cp "$SRC_DISK" "$WORK_DISK"
fi

if [ -t 0 ]; then
  echo "JamesBrown on gep9 + foggynet (ssid: simulated): ^C goes to OS-9; quit tether with ^] ." >&2
fi

exec "$TETHER" \
  -wire="exec:'$GEP9_BIN' --jamesbrown --tether-stdio -disk0 '$WORK_DISK'" \
  -pc "$PC_DIR" \
  -pass_ctrl_c \
  -level=2 \
  "$@"
