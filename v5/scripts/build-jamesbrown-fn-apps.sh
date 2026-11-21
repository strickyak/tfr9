#!/bin/sh
# build-jamesbrown-fn-apps.sh -- compile the 5 CMOC C FujiNet apps (fnnews, fnstocks,
# fnweather, fnbounce, fnbattleship) entirely inside v5/build/fn-apps/, without writing
# to ~/github.com/dillera/tfr9 (RULE ONE).
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"

L2_DIR=${DILLERA_L2_DIR:-$HOME/github.com/dillera/tfr9/v4/nitros9-l2}
FN_ROOT=${FN_SRC_DIR:-$L2_DIR/fujinet}
FNLIB="$FN_ROOT/fujinet-lib"
FN_C="$FN_ROOT/c"
BUS_CALL_C="$V5/os/nitros9/fujinet/fuji_bus_call.c"

OUT_DIR=${1:-$V5/build/fn-apps}

if [ ! -d "$FN_ROOT" ]; then
  echo "error: missing FujiNet C sources at $FN_ROOT" >&2
  exit 1
fi
if ! command -v cmoc >/dev/null 2>&1; then
  echo "error: cmoc not found on PATH" >&2
  exit 1
fi
if ! command -v lwar >/dev/null 2>&1; then
  echo "error: lwar not found on PATH" >&2
  exit 1
fi

mkdir -p "$OUT_DIR/fnlib" "$OUT_DIR/bin" "$OUT_DIR/obj" "$OUT_DIR/work"
cd "$OUT_DIR/work"
BUILD_DATE=$(date +%Y-%m-%d)

CPP_WRAP="$FNLIB/cmoc-cpp"
FNLIB_BASE_CFLAGS="--os9 --cpp=$CPP_WRAP -I$FNLIB/include"
FNLIB_CFLAGS="$FNLIB_BASE_CFLAGS -I$FNLIB/bus/os9"
APP_CFLAGS="$FNLIB_BASE_CFLAGS --function-stack=0 --add-os9-stack-space=2048 -DBUILD_OS9 -I$FN_C"

# 1. Build libfujinet.a in $OUT_DIR/fnlib/libfujinet.a
rm -f "$OUT_DIR/fnlib/"*.o "$OUT_DIR/fnlib/libfujinet.a"
for src in "$FNLIB"/common/*.c "$FNLIB"/bus/os9/*.c "$BUS_CALL_C"; do
  base=$(basename "$src" .c)
  cmoc $FNLIB_CFLAGS -c -o "$OUT_DIR/fnlib/$base.o" "$src"
done
lwar -a -r "$OUT_DIR/fnlib/libfujinet.a" "$OUT_DIR/fnlib/"*.o

# 2. Shared terminal module (term.c)
cmoc $APP_CFLAGS -c -o "$OUT_DIR/obj/term.o" "$FN_C/term.c"

# 3. fnnews
mkdir -p "$OUT_DIR/obj/news"
rm -f "$OUT_DIR/obj/news/"*.o
for src in "$FN_ROOT"/news/*.c; do
  base=$(basename "$src" .c)
  cmoc $APP_CFLAGS -c -o "$OUT_DIR/obj/news/$base.o" "$src"
done
(cd "$OUT_DIR/bin" && cmoc --os9 -o fnnews "$OUT_DIR/obj/news/"*.o "$OUT_DIR/obj/term.o" -L"$OUT_DIR/fnlib" -lfujinet)

# 4. fnweather
mkdir -p "$OUT_DIR/obj/weather"
rm -f "$OUT_DIR/obj/weather/"*.o
for src in "$FN_ROOT"/weather/*.c; do
  base=$(basename "$src" .c)
  cmoc $APP_CFLAGS -c -o "$OUT_DIR/obj/weather/$base.o" "$src"
done
(cd "$OUT_DIR/bin" && cmoc --os9 -o fnweather "$OUT_DIR/obj/weather/"*.o "$OUT_DIR/obj/term.o" -L"$OUT_DIR/fnlib" -lfujinet)

# 5. fnstocks
mkdir -p "$OUT_DIR/obj/stocks"
rm -f "$OUT_DIR/obj/stocks/"*.o
STOCKS_VER="1ececd8"
for src in "$FN_ROOT"/stocks/src/*.c "$FN_ROOT"/stocks/src/os9/*.c; do
  tag=$(printf '%s' "$src" | sed 's|^.*/stocks/src/||; s|/|_|g; s|\.c$||')
  cmoc $APP_CFLAGS -I"$FN_ROOT/stocks/include" -I"$FN_ROOT/stocks/include/os9" \
    "-DGIT_VERSION=\"$STOCKS_VER\"" \
    -c -o "$OUT_DIR/obj/stocks/$tag.o" "$src"
done
(cd "$OUT_DIR/bin" && cmoc --os9 -o fnstocks "$OUT_DIR/obj/stocks/"*.o "$OUT_DIR/obj/term.o" -L"$OUT_DIR/fnlib" -lfujinet)

# 6. fnbounce
mkdir -p "$OUT_DIR/obj/bounce"
rm -f "$OUT_DIR/obj/bounce/"*.o
BOUNCE_VER="5ac9408"
for src in "$FN_ROOT/bounce/src/main.c" "$FN_ROOT"/bounce/src/common/*.c "$FN_ROOT"/bounce/src/os9/*.c; do
  tag=$(printf '%s' "$src" | sed 's|^.*/bounce/src/||; s|/|_|g; s|\.c$||')
  cmoc $APP_CFLAGS -I"$FN_ROOT/bounce/src/include" -I"$FN_ROOT/bounce/src/os9" \
    "-DBUILD_DATE=\"$BUILD_DATE\"" "-DUPSTREAM_VERSION=\"$BOUNCE_VER\"" \
    -c -o "$OUT_DIR/obj/bounce/$tag.o" "$src"
done
(cd "$OUT_DIR/bin" && cmoc --os9 -o fnbounce "$OUT_DIR/obj/bounce/"*.o "$OUT_DIR/obj/term.o" -L"$OUT_DIR/fnlib" -lfujinet)

# 7. fnbattleship
mkdir -p "$OUT_DIR/obj/battleship"
rm -f "$OUT_DIR/obj/battleship/"*.o
BSHIP_VER="87b8cc7"
for src in "$FN_ROOT"/battleship/src/*.c "$FN_ROOT"/battleship/src/os9/*.c; do
  tag=$(printf '%s' "$src" | sed 's|^.*/battleship/src/||; s|/|_|g; s|\.c$||')
  cmoc $APP_CFLAGS -I"$FN_ROOT/battleship/src" -I"$FN_ROOT/battleship/src/os9" \
    "-DBUILD_DATE=\"$BUILD_DATE\"" "-DUPSTREAM_VERSION=\"$BSHIP_VER\"" \
    -c -o "$OUT_DIR/obj/battleship/$tag.o" "$src"
done
(cd "$OUT_DIR/bin" && cmoc --os9 -o fnbattleship "$OUT_DIR/obj/battleship/"*.o "$OUT_DIR/obj/term.o" -L"$OUT_DIR/fnlib" -lfujinet)

echo "built FujiNet C apps in $OUT_DIR/bin: fnnews fnweather fnstocks fnbounce fnbattleship"
