#!/bin/sh
# 06_engines_test.sh -- boot each guest OS on the board and talk to it (expect).  Ports v4 systests 03-06.
#   Turbo9OS needs no files (its ROM is built in).  The others need images, found from environment variables,
#   with defaults inside the coco-shelf checkout; a missing image SKIPS that engine:
#     V5_IMG_FLAT_BOOT  (default $SHELF/nitros9/recipes/flat65280/flat65280.img)   64 KB NitrOS-9 L1 boot image
#     V5_IMG_FLAT_DISK  (default $SHELF/nitros9/recipes/flat65280/flat0.dsk)
#     V5_IMG_DEEP_BOOT  (default $SHELF/nitros9/recipes/deep65280/deep65280.img)   NitrOS-9 L2 boot image
#     V5_IMG_DEEP_DISK  (default $SHELF/nitros9/recipes/deep65280/deep0.dsk)
#     V5_IMG_HATVAN_DIR (default $HOME/github.com/strickyak/hatvan-os/build)       Hatvan OS kernel/rbf/procfs/disk0
#     V5_IMG_FUZIX_BIN, V5_IMG_FUZIX_DISK  (no default)  Fuzix kernel and root disk for the multicomp09fuzix engine
#   Images are COPIED into a temporary /pc (tether refuses symlinks there).  ENGINES="turbo9os flat65280" selects.
. "$(dirname "$0")/common.sh"
echo "Running Suite 6: guest operating systems (engines)"
if ! board_present; then skip "whole suite (no board attached)"; print_summary "Suite 6 (engines)"; exit $?; fi
if ! command -v expect > /dev/null 2>&1; then skip "whole suite (no expect command)"; print_summary "Suite 6 (engines)"; exit $?; fi

SHELF=${SHELF:-$(cd "$V5/../.." && pwd)}
F_BOOT=${V5_IMG_FLAT_BOOT:-$SHELF/nitros9/recipes/flat65280/flat65280.img}
F_DISK=${V5_IMG_FLAT_DISK:-$SHELF/nitros9/recipes/flat65280/flat0.dsk}
D_BOOT=${V5_IMG_DEEP_BOOT:-$SHELF/nitros9/recipes/deep65280/deep65280.img}
D_DISK=${V5_IMG_DEEP_DISK:-$SHELF/nitros9/recipes/deep65280/deep0.dsk}
H_DIR=${V5_IMG_HATVAN_DIR:-$HOME/github.com/strickyak/hatvan-os/build}
X_BIN=${V5_IMG_FUZIX_BIN:-}
X_DISK=${V5_IMG_FUZIX_DISK:-}

PC_DIR=$(mktemp -d "${TMPDIR:-/tmp}/systest_pc.XXXXXX")
trap 'rm -rf "$PC_DIR"' EXIT INT TERM

# NOTE: the board may carry its own /rc/modeN.tcl from earlier firmware (it wins over /pc/modeN.tcl); those
# expect /pc/disk0.dsk, so each engine also gets a disk0.dsk copy.
# stage NAME FILE... : copy the fixtures (mode/boot scripts) and the given image files into a fresh /pc.
# Each image is given as  source:destname .  Returns 1 (after saying why) if a source is missing.
stage() {
  rm -rf "$PC_DIR"; mkdir -p "$PC_DIR"
  cp "$SYSTEST_DIR"/pcfix/*.tcl "$PC_DIR"/ || return 1
  for pair in "$@"; do
    src=${pair%%:*}; dst=${pair#*:}
    if [ -z "$src" ] || [ ! -f "$src" ]; then echo "  (missing image: ${src:-unset} for $dst)"; return 1; fi
    cp "$src" "$PC_DIR/$dst" || return 1
  done
}

run_engine() {   # run_engine NAME DESCRIPTION [source:dest ...]
  name=$1; desc=$2; shift 2
  case " ${ENGINES:-turbo9os flat65280 deep65280 gep9 fuzix} " in *" $name "*) ;; *) return 0 ;; esac
  log_test "$desc"
  # The board must answer an RPC before we start: it may still be rebooting or re-enumerating after the
  # previous engine, and an expect session started too early just hangs.
  _ok=0; _try=0
  while [ $_try -lt 8 ]; do
    run_tether --just-ping=1
    case "$TETHER_STDOUT" in *"just-ping: OK"*) _ok=1; break ;; esac
    sleep 2; _try=$((_try + 1))
  done
  [ $_ok -eq 1 ] || { fail "$name: board does not answer ping before the test"; return 0; }
  if ! stage "$@"; then skip "$name (images not available; see the variables at the top of this file)"; return 0; fi
  out=$(TETHER="$TETHER" PC_DIR="$PC_DIR" WIRE="$WIRE" expect "$SYSTEST_DIR/engines/$name.exp" < /dev/null 2>&1)
  rc=$?
  mkdir -p "$BUILD_DIR/systest-logs" && printf '%s\n' "$out" > "$BUILD_DIR/systest-logs/engine-$name.log"   # full transcript, for debugging
  printf '%s\n' "$out" | grep -E "PASS|FAIL|Timed out" | sed 's/^/  /'
  if [ $rc -eq 0 ]; then pass "$name engine"; else fail "$name engine (expect exit $rc)"; printf '%s\n' "$out" | tail -15 >&2; fi
  # the board reboots after some engines stop; find it again by label before the next one
  _try=0; WIRE=
  while [ -z "$WIRE" ] && [ $_try -lt 20 ]; do find_wire; [ -n "$WIRE" ] || sleep 1; _try=$((_try + 1)); done
  [ -n "$WIRE" ] || { fail "board did not come back after $name"; return 1; }
}

run_engine turbo9os  "Engine 0: Turbo9OS (built-in)"
run_engine flat65280 "Engine 1: Flat65280 (NitrOS-9 Level 1)" "$F_BOOT:flat65280.img" "$F_DISK:flat0.dsk" "$F_DISK:disk0.dsk"
run_engine deep65280 "Engine 2: Deep65280 (NitrOS-9 Level 2, 256 KB MMU)" "$D_BOOT:deep65280.img" "$D_DISK:deep0.dsk" "$D_DISK:disk0.dsk"
run_engine gep9      "Engine 3: Hatvan OS (gep9, EMBIGGEN paged MMU)" "$H_DIR/kernel_6809.decb:kernel_6809.decb" "$H_DIR/rbf_6809.decb:rbf_6809.decb" "$H_DIR/procfs_6809.decb:procfs_6809.decb" "$H_DIR/disk0.dsk:hatvan0.dsk" "$H_DIR/disk0.dsk:disk0.dsk"
run_engine fuzix     "Engine 4: multicomp09fuzix (Fuzix 6809)" "$X_BIN:fuzix.bin" "$X_DISK:disk.img"

print_summary "Suite 6 (engines)"
