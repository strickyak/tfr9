#!/bin/sh
# make doctor -- check the v5 toolchain.  POSIX sh (works with dash and macOS /bin/sh).
# PASS/WARN/FAIL with a hint for each; exit status is non-zero if any FAIL.
V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"
fails=0; warns=0
pass() { printf '  PASS  %-22s %s\n' "$1" "$2"; }
warn() { printf '  WARN  %-22s %s\n         -> %s\n' "$1" "$2" "$3"; warns=$((warns+1)); }
fail() { printf '  FAIL  %-22s %s\n         -> %s\n' "$1" "$2" "$3"; fails=$((fails+1)); }
have() { command -v "$1" >/dev/null 2>&1; }
# ver_ge HAVE WANT : true if dotted version HAVE >= WANT  (awk; no sort -V, which old macOS lacks)
ver_ge() {
  awk -v a="$1" -v b="$2" 'BEGIN {
    n = split(a, x, "."); m = split(b, y, ".")
    for (i = 1; i <= (n > m ? n : m); i++) { u = x[i] + 0; v = y[i] + 0; if (u > v) exit 0; if (u < v) exit 1 }
    exit 0 }'
}

echo "v5 doctor   V5=$V5"
echo "            SHELF=$SHELF"
echo "            build dir: $V5_BUILD"

# --- shelf and Pico SDK -------------------------------------------------
if [ -d "$SHELF" ]; then pass SHELF "$SHELF"; else fail SHELF "$SHELF missing" "set SHELF=/path/to/coco-shelf"; fi
if [ -n "$V5_AMBIENT_PICO_SDK_PATH" ] && [ "$V5_AMBIENT_PICO_SDK_PATH" != "$PICO_SDK_PATH" ]; then
  warn "ambient PICO_SDK_PATH" "$V5_AMBIENT_PICO_SDK_PATH" "v5 ignores it and uses $PICO_SDK_PATH (set V5_PICO_SDK to change)"
fi
if [ -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
  f="$PICO_SDK_PATH/pico_sdk_version.cmake"
  maj=$(sed -n 's/.*PICO_SDK_VERSION_MAJOR \([0-9][0-9]*\)).*/\1/p' "$f" | head -n 1)
  min=$(sed -n 's/.*PICO_SDK_VERSION_MINOR \([0-9][0-9]*\)).*/\1/p' "$f" | head -n 1)
  rev=$(sed -n 's/.*PICO_SDK_VERSION_REVISION \([0-9][0-9]*\)).*/\1/p' "$f" | head -n 1)
  v="$maj.$min.$rev"
  if [ "$v" = "2.3.0" ]; then pass pico-sdk "$v ($PICO_SDK_PATH)"
  else warn pico-sdk "found $v at $PICO_SDK_PATH, v4 used 2.3.0" "use 2.3.0 for parity"; fi
  if [ -f "$PICO_SDK_PATH/lib/tinyusb/src/tusb.h" ]; then pass "pico-sdk submodules" "tinyusb present"
  else fail "pico-sdk submodules" "lib/tinyusb missing" "cd $PICO_SDK_PATH && git submodule update --init"; fi
else
  fail pico-sdk "no pico_sdk_init.cmake in $PICO_SDK_PATH" "install pico-sdk 2.3.0 with submodules into \$SHELF/pico-sdk"
fi
if [ -f "$picotool_DIR/picotoolConfig.cmake" ]; then pass "picotool (cmake)" "$picotool_DIR"
else fail "picotool (cmake)" "no picotoolConfig.cmake in $picotool_DIR" "install picotool with CMAKE_INSTALL_PREFIX=\$SHELF, or set V5_PICOTOOL_DIR"; fi
if have picotool; then pass "picotool (exe)" "$(picotool version 2>&1 | head -n 1)"
else warn "picotool (exe)" "not on PATH" "needed to flash from the command line"; fi

# --- compilers and build tools -----------------------------------------
if have arm-none-eabi-gcc; then pass arm-none-eabi-gcc "$(arm-none-eabi-gcc -dumpfullversion 2>/dev/null || arm-none-eabi-gcc -dumpversion)"
else fail arm-none-eabi-gcc missing "install the Arm GNU toolchain (apt: gcc-arm-none-eabi; macOS: brew install --cask gcc-arm-embedded)"; fi
if have arm-none-eabi-g++; then pass arm-none-eabi-g++ ok
else fail arm-none-eabi-g++ missing "install the Arm GNU toolchain with C++ support"; fi
if have cmake; then
  v=$(cmake --version | sed -n '1s/.* //p')
  if ver_ge "$v" 3.13; then pass cmake "$v"; else fail cmake "$v < 3.13" "upgrade cmake"; fi
else fail cmake missing "install cmake"; fi
if have make; then pass make "$(make --version 2>&1 | head -n 1)"; else fail make missing "install make"; fi
if have git; then pass git "$(git --version | cut -d' ' -f3)"; else fail git missing "install git"; fi
if have go; then
  v=$(go env GOVERSION | sed 's/^go//')
  if ver_ge "$v" 1.25; then pass go "$v"; else warn go "$v (< 1.25)" "v4's go.mod says 1.25.1"; fi
else fail go missing "install Go >= 1.25 (tether)"; fi
if have python3; then pass python3 "$(python3 --version 2>&1 | cut -d' ' -f2)"; else warn python3 missing "used by generators and a few scripts"; fi
if have tclsh; then pass tclsh ok; else warn tclsh missing "needed to generate NitrOS-9 makefiles (os/)"; fi
if have expect; then pass expect ok; else warn expect missing "needed by hardware systests"; fi

# --- 6809 toolchain (guest software; needed from Phase 5) --------------
for t in lwasm lwlink lwar; do
  if have "$t"; then pass "$t" "$("$t" --version 2>&1 | grep -i lwtools | head -n 1)"
  else warn "$t" missing "LWTOOLS; only needed to build os/ software"; fi
done
if have cmoc; then pass cmoc "$(cmoc --version 2>&1 | head -n 1)"; else warn cmoc missing "CMOC; only for C programs in os/"; fi
if have os9; then pass "os9 (toolshed)" ok; else warn "os9 (toolshed)" missing "toolshed; only for building NitrOS-9 disk images"; fi

# --- hygiene ------------------------------------------------------------
if [ -d "$V5/../v4" ]; then pass "v4 present" "read-only reference at ../v4"
else warn "v4 present" "no ../v4" "only needed by scripts/import-from-v4.sh"; fi
b=$(git -C "$V5" branch --show-current 2>/dev/null)
case "$b" in
  work*) pass "git branch" "$b" ;;
  *)     warn "git branch" "'$b'" "commit only on work* branches (AGENTS.md)" ;;
esac

echo
echo "doctor: $fails FAIL, $warns WARN"
[ "$fails" -eq 0 ]
