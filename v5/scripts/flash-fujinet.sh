#!/bin/sh
# flash-fujinet.sh [--port=DEV] [--keep-fs] [--rebuild]
#
# Build (if needed) and burn the CoCo Centipede / TFR/911H FujiNet firmware
# (`fujinet-coco-devkitc-centipede`, 921600 baud UART1 RX=IO33 TX=IO21, IO13 hi-Z)
# onto an ESP32 DevKitC FujiNet hat over its USB-UART cable.
#
# By default, burns all four images needed by a brand-new 8 MB ESP32:
#   0x001000  bootloader.bin
#   0x008000  partitions.bin
#   0x010000  firmware.bin
#   0x570000  littlefs.bin  (WebUI, /autorun.dsk, /ca.pem, default config)
#
# Pass --keep-fs (or KEEP_FS=1) when updating an existing FujiNet card to
# preserve its LittleFS storage partition (nvs at 0x9000 is always preserved).
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
SHELF=${SHELF:-$(cd "$V5/../.." && pwd)}
FN_DIR=${FUJINET_FIRMWARE_DIR:-$SHELF/fujinet-firmware}
FN_ENV=${FUJINET_ENV:-fujinet-coco-devkitc-centipede}
PORT=${PORT:-${FUJINET_PORT:-}}
KEEP_FS=${KEEP_FS:-0}
REBUILD=${REBUILD:-0}
BAUD=${FUJINET_FLASH_BAUD:-460800}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --port=*)
      PORT=${1#--port=}
      shift
      ;;
    --keep-fs)
      KEEP_FS=1
      shift
      ;;
    --rebuild)
      REBUILD=1
      shift
      ;;
    --help|-h)
      sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "flash-fujinet.sh: unknown option '$1' (try --help)" >&2
      exit 2
      ;;
  esac
done

# Locate ESP32 USB-UART port (never /dev/ttyACM* or /dev/cu.usbmodem*, which are RP2350 Pico boards).
if [ -z "$PORT" ]; then
  candidates=""
  count=0
  for dev in /dev/ttyUSB* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
    if [ -e "$dev" ]; then
      candidates="$candidates $dev"
      count=$((count + 1))
    fi
  done
  if [ "$count" -eq 0 ]; then
    echo "error: no ESP32 USB-UART device (/dev/ttyUSB* or /dev/cu.usbserial*) found." >&2
    echo "Plug a USB data cable into the FujiNet hat's ESP32 port, or pass PORT=/dev/..." >&2
    exit 1
  fi
  if [ "$count" -gt 1 ]; then
    echo "error: multiple USB-UART devices found:$candidates" >&2
    if [ -d /dev/serial/by-id ]; then
      ls -l /dev/serial/by-id/ >&2 || true
    fi
    echo "Specify which one to flash: make flash-fujinet PORT=/dev/ttyUSB0" >&2
    exit 1
  fi
  # Trim leading space.
  PORT=${candidates# }
fi

if [ ! -d "$FN_DIR" ]; then
  echo "error: fujinet-firmware repo not found at $FN_DIR (set FUJINET_FIRMWARE_DIR=...)" >&2
  exit 1
fi

PIO_BIN="$FN_DIR/build/.venv/bin/pio"
PY_BIN="$FN_DIR/build/.venv/bin/python3"
if [ ! -x "$PIO_BIN" ]; then
  PIO_BIN=$(command -v pio 2>/dev/null || true)
  PY_BIN=$(command -v python3 2>/dev/null || true)
fi

BUILD_DIR="$FN_DIR/.pio/build/$FN_ENV"

need_fw=0
if [ "$REBUILD" = "1" ] || [ ! -f "$BUILD_DIR/bootloader.bin" ] || \
   [ ! -f "$BUILD_DIR/partitions.bin" ] || [ ! -f "$BUILD_DIR/firmware.bin" ]; then
  need_fw=1
fi

need_fs=0
if [ "$KEEP_FS" != "1" ] && { [ "$REBUILD" = "1" ] || [ ! -f "$BUILD_DIR/littlefs.bin" ]; }; then
  need_fs=1
fi

if [ "$need_fw" -eq 1 ] || [ "$need_fs" -eq 1 ]; then
  if [ -z "$PIO_BIN" ] || [ ! -x "$PIO_BIN" ]; then
    echo "error: PlatformIO ($FN_DIR/build/.venv/bin/pio) not found; run ./build.sh -s $FN_ENV in $FN_DIR" >&2
    exit 1
  fi
  (
    cd "$FN_DIR"
    if [ ! -f platformio-generated.ini ]; then
      "$PY_BIN" create-platformio-ini.py -n "$FN_ENV" -o platformio-generated.ini -l platformio.local.ini
    fi
    if [ "$need_fw" -eq 1 ]; then
      echo "=== Building $FN_ENV firmware ==="
      "$PIO_BIN" run -c platformio-generated.ini -e "$FN_ENV"
    fi
    if [ "$need_fs" -eq 1 ]; then
      echo "=== Building $FN_ENV LittleFS image ==="
      "$PIO_BIN" run -c platformio-generated.ini -e "$FN_ENV" -t buildfs
    fi
  )
fi

# Locate esptool.
if [ -n "${ESPTOOL:-}" ]; then
  set -- "$ESPTOOL"
elif [ -x "$HOME/.platformio/packages/tool-esptool/esptool" ]; then
  set -- "$HOME/.platformio/packages/tool-esptool/esptool"
elif command -v esptool >/dev/null 2>&1; then
  set -- "$(command -v esptool)"
elif [ -f "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" ]; then
  set -- python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
elif command -v esptool.py >/dev/null 2>&1; then
  set -- "$(command -v esptool.py)"
else
  echo "error: esptool not found in ~/.platformio or PATH" >&2
  exit 1
fi

echo "=== Flashing $FN_ENV onto $PORT (baud $BAUD, keep_fs=$KEEP_FS) ==="
if [ -d /dev/serial/by-id ]; then
  ls -l /dev/serial/by-id/ 2>/dev/null | grep "$(basename "$PORT")$" || true
fi

if [ "$KEEP_FS" = "1" ]; then
  "$@" --port "$PORT" --baud "$BAUD" write-flash \
    0x1000  "$BUILD_DIR/bootloader.bin" \
    0x8000  "$BUILD_DIR/partitions.bin" \
    0x10000 "$BUILD_DIR/firmware.bin"
else
  "$@" --port "$PORT" --baud "$BAUD" write-flash \
    0x1000   "$BUILD_DIR/bootloader.bin" \
    0x8000   "$BUILD_DIR/partitions.bin" \
    0x10000  "$BUILD_DIR/firmware.bin" \
    0x570000 "$BUILD_DIR/littlefs.bin"
fi

echo "=== Flash complete on $PORT ==="
