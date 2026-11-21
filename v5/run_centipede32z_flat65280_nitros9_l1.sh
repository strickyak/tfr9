#!/bin/sh
# run_centipede32z_flat65280_nitros9_l1.sh - Launch NitrOS-9 Level 1 (flat65280) on CoCo 2 (-coco2, default) or CoCo 3 (-coco3) via Centipede 32z.
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

if [ -n "${V5_IMG_FLAT_BOOT:-}" ]; then
  BOOT=$V5_IMG_FLAT_BOOT
elif [ -f "$DIR/images/flat65280/flat65280.img" ]; then
  BOOT="$DIR/images/flat65280/flat65280.img"
else
  BOOT="$SHELF/nitros9/recipes/flat65280/flat65280.img"
fi

if [ -n "${V5_IMG_FLAT_DISK:-}" ]; then
  DISK=$V5_IMG_FLAT_DISK
elif [ -f "$DIR/images/flat65280/flat0.dsk" ]; then
  DISK="$DIR/images/flat65280/flat0.dsk"
else
  DISK="$SHELF/nitros9/recipes/flat65280/flat0.dsk"
fi

if [ ! -f "$BOOT" ]; then
  echo "error: missing flat65280 boot image: $BOOT" >&2
  exit 1
fi
if [ ! -f "$DISK" ]; then
  echo "error: missing flat0 disk image: $DISK" >&2
  exit 1
fi

PC_DIR="$DIR/build/demo-pc/flat65280_32z"
mkdir -p "$PC_DIR"
cp "$BOOT" "$PC_DIR/flat65280.img"
if [ ! -f "$PC_DIR/flat0.dsk" ] || [ -n "$(find "$DISK" -newer "$PC_DIR/flat0.dsk" 2>/dev/null)" ]; then
  cp "$DISK" "$PC_DIR/flat0.dsk"
fi

BOOTMODE=9031
cat > "$PC_DIR/mode${BOOTMODE}.tcl" << EOF
on $HOST_COCO run nitros9l1 for flat65280
set Config(flat_img) "/pc/flat65280.img"
set Config(drive0) "/pc/flat0.dsk"
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
  exec "$TETHER" -centipede -wire="$WIRE" -pc "$PC_DIR" -bootmode="$BOOTMODE" -level=1 "$@"
else
  exec "$TETHER" -centipede -pc "$PC_DIR" -bootmode="$BOOTMODE" -level=1 "$@"
fi
