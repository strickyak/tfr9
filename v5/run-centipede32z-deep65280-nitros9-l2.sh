#!/bin/sh
# run-centipede32z-deep65280-nitros9-l2.sh - Launch NitrOS-9 Level 2 (deep65280) on CoCo 2 (-coco2, default) or CoCo 3 (-coco3) via Centipede 32z.
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")
SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}

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

if [ -n "${V5_IMG_DEEP_BOOT:-}" ]; then
  BOOT=$V5_IMG_DEEP_BOOT
elif [ -f "$DIR/images/deep65280/deep65280.img" ]; then
  BOOT="$DIR/images/deep65280/deep65280.img"
else
  BOOT="$SHELF/nitros9/recipes/deep65280/deep65280.img"
fi

if [ -n "${V5_IMG_DEEP_DISK:-}" ]; then
  DISK=$V5_IMG_DEEP_DISK
elif [ -f "$DIR/images/deep65280/deep0.dsk" ]; then
  DISK="$DIR/images/deep65280/deep0.dsk"
else
  DISK="$SHELF/nitros9/recipes/deep65280/deep0.dsk"
fi

if [ ! -f "$BOOT" ]; then
  echo "error: missing deep65280 boot image: $BOOT" >&2
  exit 1
fi
if [ ! -f "$DISK" ]; then
  echo "error: missing deep0 disk image: $DISK" >&2
  exit 1
fi

PC_DIR="$DIR/build/demo-pc/deep65280_32z"
mkdir -p "$PC_DIR"
cp "$BOOT" "$PC_DIR/deep65280.img"
if [ ! -f "$PC_DIR/deep0.dsk" ] || [ -n "$(find "$DISK" -newer "$PC_DIR/deep0.dsk" 2>/dev/null)" ]; then
  cp "$DISK" "$PC_DIR/deep0.dsk"
fi

BOOTMODE=9032
cat > "$PC_DIR/mode${BOOTMODE}.tcl" << EOF
on $HOST_COCO run nitros9l2 for deep65280
set Config(flat_img) "/pc/deep65280.img"
set Config(drive0) "/pc/deep0.dsk"
set Config(drive1) ""
bye
EOF

has_wire=0
for arg in "$@"; do
  case "$arg" in
    -wire|-wire=*|--wire|--wire=*) has_wire=1 ;;
  esac
done

if [ "$has_wire" -eq 0 ]; then
  if [ -z "${WIRE:-}" ]; then
    WIRE=$("$DIR/scripts/find-board.sh" b=centipede v=32z 2>/dev/null || true)
  fi
  if [ -z "${WIRE:-}" ]; then
    echo "error: Centipede 32z board (b=centipede v=32z) not found; plug it in or pass -wire=..." >&2
    exit 1
  fi
  exec "$TETHER" -centipede -wire="$WIRE" -pc "$PC_DIR" -bootmode="$BOOTMODE" -level=2 "$@"
else
  exec "$TETHER" -centipede -pc "$PC_DIR" -bootmode="$BOOTMODE" -level=2 "$@"
fi
