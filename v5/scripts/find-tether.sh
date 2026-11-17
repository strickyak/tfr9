#!/bin/sh
# find-tether.sh -- print path to an executable tether binary for this host (POSIX sh).
# Resolution order:
#   1. $TETHER environment override (if set)
#   2. $ROOT/build/tether             (developer checkout after `make tether`)
#   3. $ROOT/bin/tether.<plat>.exe    (pre-compiled binary release)
#   4. $ROOT/build/tether.<plat>.exe  (developer checkout after `make tethers`)
#   5. $ROOT/tether.<plat>.exe
set -eu

if [ -n "${TETHER:-}" ]; then
  if [ -x "$TETHER" ]; then
    printf '%s\n' "$TETHER"
    exit 0
  fi
  echo "find-tether: TETHER='$TETHER' is not executable" >&2
  exit 2
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ -x "$ROOT/build/tether" ]; then
  printf '%s\n' "$ROOT/build/tether"
  exit 0
fi

os=$(uname -s 2>/dev/null || echo unknown)
arch=$(uname -m 2>/dev/null || echo unknown)
plat=""

case "$os" in
  Linux)
    case "$arch" in
      x86_64|amd64)       plat="linux-amd64" ;;
      i386|i486|i586|i686|x86) plat="linux-386" ;;
      aarch64|arm64)      plat="linux-arm64" ;;
      armv7*|armv8l|arm*) plat="linux-arm-7" ;;
    esac
    ;;
  Darwin)
    case "$arch" in
      arm64)  plat="mac-arm64" ;;
      x86_64) plat="mac-amd64" ;;
    esac
    ;;
  MINGW*|MSYS*|CYGWIN*)
    case "$arch" in
      x86_64|amd64) plat="win-amd64" ;;
      *86)          plat="win-386" ;;
    esac
    ;;
esac

if [ -n "$plat" ]; then
  for cand in \
    "$ROOT/bin/tether.$plat.exe" \
    "$ROOT/build/tether.$plat.exe" \
    "$ROOT/tether.$plat.exe"
  do
    if [ -x "$cand" ]; then
      printf '%s\n' "$cand"
      exit 0
    fi
    if [ -f "$cand" ]; then
      echo "find-tether: found $cand, but it is not marked executable (run: chmod +x \"$cand\")" >&2
      exit 2
    fi
  done
fi

echo "find-tether: no tether binary found for $os/$arch under $ROOT (run 'make tether', or check bin/tether.*.exe)" >&2
if [ "$os" = "Darwin" ]; then
  echo "find-tether: on macOS, if Gatekeeper blocks downloaded binaries, run: xattr -dr com.apple.quarantine \"$ROOT\"" >&2
fi
exit 2
