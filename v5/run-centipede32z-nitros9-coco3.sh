#!/bin/sh
# run-centipede32z-nitros9-coco3.sh - Launch NitrOS-9 Level 2 (coco3 target) on CoCo 2 (-coco2, default) or CoCo 3 (-coco3) via Centipede 32z.
set -eu

DIR=$(cd "$(dirname "$0")" && pwd)
TETHER=$("$DIR/scripts/find-tether.sh")
SHELF=${SHELF:-$(cd "$DIR/../.." && pwd)}

HOST_COCO=coco2
if [ "${1:-}" = "-coco3" ] || [ "${1:-}" = "--coco3" ]; then
  HOST_COCO=coco3
  shift
elif [ "${1:-}" = "-coco2" ] || [ "${1:-}" = "--coco2" ]; then
  HOST_COCO=coco2
  shift
fi

if [ -n "${V5_IMG_COCO3_DISK:-}" ]; then
  DISK=$V5_IMG_COCO3_DISK
elif [ -f "$DIR/images/coco3/coco3.dsk" ]; then
  DISK="$DIR/images/coco3/coco3.dsk"
elif [ -f "$DIR/build/coco3.dsk" ]; then
  DISK="$DIR/build/coco3.dsk"
else
  DISK="$SHELF/nitros9/recipes/coco3/dw/emudsk.dsk"
fi

if [ ! -f "$DISK" ]; then
  echo "error: missing NitrOS-9 coco3 disk image: $DISK (set V5_IMG_COCO3_DISK=/path/to/coco3.dsk)" >&2
  exit 1
fi

BOOT_LINE='set Config(flat_img) ""'
PC_DIR="$DIR/build/demo-pc/nitros9_coco3_32z"
mkdir -p "$PC_DIR"
if [ -n "${V5_IMG_COCO3_BOOT:-}" ]; then
  if [ ! -f "$V5_IMG_COCO3_BOOT" ]; then
    echo "error: missing NitrOS-9 coco3 boot image: $V5_IMG_COCO3_BOOT" >&2
    exit 1
  fi
  cp "$V5_IMG_COCO3_BOOT" "$PC_DIR/coco3.img"
  BOOT_LINE='set Config(flat_img) "/pc/coco3.img"'
fi

if [ ! -f "$PC_DIR/coco3.dsk" ] || [ -n "$(find "$DISK" -newer "$PC_DIR/coco3.dsk" 2>/dev/null)" ]; then
  cp "$DISK" "$PC_DIR/coco3.dsk"
fi

BOOTMODE=9033
cat > "$PC_DIR/mode${BOOTMODE}.tcl" << EOF
on $HOST_COCO run nitros9l2 for coco3
$BOOT_LINE
set Config(drive0) "/pc/coco3.dsk"
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
