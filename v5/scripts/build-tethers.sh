#!/bin/sh
# build-tethers.sh [native|all]   -- build the Go tether into build/ (POSIX sh).
#   native : build/tether                       for this machine
#   all    : build/tether.<os>-<arch>.exe       the 8 static cross builds v4 shipped
V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"
what=${1:-native}
mkdir -p "$V5_BUILD"
cd "$V5/host" || exit 1
GOWORK=off; export GOWORK
CGO_ENABLED=0; export CGO_ENABLED
build() {   # build GOOS GOARCH [GOARM] OUT
  GOOS=$1 GOARCH=$2 GOARM=${3:-} go build -a -ldflags '-extldflags "-static"' -o "$V5_BUILD/$4" ./cmd/tether || exit 1
  echo "built build/$4"
}
case $what in
  native) go build -o "$V5_BUILD/tether" ./cmd/tether && echo "built build/tether" ;;
  all)
    build linux   amd64 ""  tether.linux-amd64.exe
    build linux   386   ""  tether.linux-386.exe
    build linux   arm   7   tether.linux-arm-7.exe
    build linux   arm64 ""  tether.linux-arm64.exe
    build windows amd64 ""  tether.win-amd64.exe
    build windows 386   ""  tether.win-386.exe
    build darwin  arm64 ""  tether.mac-arm64.exe
    build darwin  amd64 ""  tether.mac-amd64.exe
    ;;
  *) echo "usage: $0 [native|all]" >&2; exit 2 ;;
esac
