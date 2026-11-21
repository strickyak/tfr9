#!/bin/sh
# run-centipede32z-jamesbrown.sh - Launch JamesBrown NitrOS-9 Level 2 + FujiNet + Basic09
# on a CoCo 2 (default, 256 KB gerbil_gime) or CoCo 3 (-coco3, gerbil_io) via Centipede 32z.
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")

HOST_COCO=coco2
nargs=$#
i=0
while [ "$i" -lt "$nargs" ]; do
  arg=$1
  shift
  case "$arg" in
    -coco3|--coco3) HOST_COCO=coco3 ;;
    -coco2|--coco2) HOST_COCO=coco2 ;;
    *) set -- "$@" "$arg" ;;
  esac
  i=$((i + 1))
done

if [ -n "${V5_IMG_JAMESBROWN_32Z_DISK:-}" ]; then
  DISK=$V5_IMG_JAMESBROWN_32Z_DISK
else
  DISK="$DIR/build/centipede32z-jamesbrown.dsk"
  if [ ! -f "$DISK" ]; then
    sh "$DIR/scripts/make-jamesbrown-32z-dsk.sh" "$DISK"
  fi
fi

PC_DIR="$DIR/build/demo-pc/jamesbrown_32z"
mkdir -p "$PC_DIR"
DST="$PC_DIR/centipede32z-jamesbrown.dsk"
if [ ! -f "$DST" ] || [ -n "$(find "$DISK" -newer "$DST" 2>/dev/null)" ]; then
  cp "$DISK" "$DST"
fi

BOOTMODE=9034
cat > "$PC_DIR/mode${BOOTMODE}.tcl" << EOF
on $HOST_COCO run nitros9l2 for jamesbrown
set Config(flat_img) ""
set Config(drive0) "/pc/centipede32z-jamesbrown.dsk"
set Config(drive1) ""
bye
EOF

has_wire=0
for arg in "$@"; do
  case "$arg" in
    -wire|-wire=*|--wire|--wire=*) has_wire=1 ;;
  esac
done

if [ -t 0 ]; then
  echo "JamesBrown-32z NitrOS-9 Level 2 ($HOST_COCO): ^C goes to OS-9; quit tether with ^] ." >&2
fi

if [ "$has_wire" -eq 0 ]; then
  if [ -z "${WIRE:-}" ]; then
    WIRE=$("$DIR/scripts/find-board.sh" b=centipede v=32z 2>/dev/null || true)
  fi
  if [ -z "${WIRE:-}" ]; then
    echo "error: Centipede 32z board (b=centipede v=32z) not found; plug it in or pass -wire=..." >&2
    exit 1
  fi
  exec "$TETHER" -centipede -wire="$WIRE" -pc "$PC_DIR" -bootmode="$BOOTMODE" -pass_ctrl_c -level=2 "$@"
else
  exec "$TETHER" -centipede -pc "$PC_DIR" -bootmode="$BOOTMODE" -pass_ctrl_c -level=2 "$@"
fi
