#!/bin/sh
# 07_ports_test.sh -- `on ... run ... for ... with ...`, `ports list|check`, and checked `bye` (spec 7, 15.y).
# Everything is verified at the TCL> prompt on real TFR/911H hardware; nothing launches the 6309.
. "$(dirname "$0")/common.sh"
echo "Running Suite 7: on/run/for/with grammar, ports list|check, and conflict reporting"
if ! board_present; then skip "whole suite (no board attached)"; print_summary "Suite 7 (ports)"; exit $?; fi

inj() { run_tether --quick-inject="$1"; }                      # sets TETHER_EXIT / TETHER_STDOUT / TETHER_STDERR
all_out() { printf '%s\n%s\n' "$TETHER_STDOUT" "$TETHER_STDERR"; }

run_tether --quick-restart=0 > /dev/null            # clean boot: settings live in RAM only

log_test "default target and minimal ports ensemble (list|check only)"
inj "on"
assert_contains "$TETHER_STDOUT" "tfr9" "default host is tfr9"
inj "ports list"
assert_contains "$TETHER_STDOUT" "on tfr9 run turbo9os for turbo9sim engine hamster_flat" "default ports list"
inj "ports check"
assert_contains "$TETHER_STDOUT" "ok" "default ports check is ok"
for gone in "ports" "ports show" "ports profile coco" "ports set becker on" "ports clear" "enhance"; do
  inj "$gone"
  assert_exit_code 1 "$TETHER_EXIT" "removed command '$gone' is an error"
done

log_test "ON ... RUN ... FOR ... WITH ... uppercase folding & key-value digestion"
inj "ON coco2 RUN nitros9l2 FOR coco3 WITH E1, becker at 0xFF41, dw over bitbang baud 9600, rtc at 0xFFE0; PORTS LIST"
assert_contains "$TETHER_STDOUT" "on coco2 run nitros9l2 for coco3 engine gerbil_gime" "compiled host/os/target/engine"
assert_contains "$TETHER_STDOUT" "with e1" "compiled enhancement map in lowercase"
assert_contains "$TETHER_STDOUT" "becker on becker.at 0xff41" "compiled becker and 0x... address in lowercase"
assert_contains "$TETHER_STDOUT" "dw on dw.over bitbang dw.baud 9600" "compiled multi-parameter dw transport"
assert_contains "$TETHER_STDOUT" "rtc on rtc.at 0xffe0" "compiled rtc override"
inj "PORTS CHECK"
assert_contains "$TETHER_STDOUT" "ok" "coco2 nitros9l2 for coco3 validates"

log_test "coco2 vs coco3 engine selection and 'on <host>' + 'run ...' shorthand"
inj "on coco3; run nitros9l2 for coco3 with e1, becker; ports list"
assert_contains "$TETHER_STDOUT" "on coco3 run nitros9l2 for coco3 engine gerbil_io" "real coco3 implies gerbil_io"
inj "ports check"
assert_contains "$TETHER_STDOUT" "ok" "coco3 nitros9l2 for coco3 validates"

log_test "every tfr9 target validates"
for pair in "turbo9os turbo9sim" "nitros9l1 flat65280" "nitros9l2 deep65280" \
            "nitros9l2 jamesbrown" "nitros9l2 coco3" \
            "hatvan gep9" "hatvan gep9big" "hatvan coco2" "fuzix multicomp09"; do
  set -- $pair
  inj "on tfr9 run $1 for $2; ports check"
  assert_contains "$TETHER_STDOUT" "ok" "tfr9: $1 for $2"
done

log_test "the known clash: gep9 and gep9big embiggen_mmu vs Becker's default address (spec 15.x)"
for f in gep9 gep9big; do
  inj "on tfr9 run hatvan for $f with e1, becker; ports check"
  assert_exit_code 1 "$TETHER_EXIT" "$f: ports check refuses default Becker"
  assert_contains "$(all_out)" "becker" "$f: message names becker"
  assert_contains "$(all_out)" "embiggen_mmu" "$f: message names embiggen_mmu"
  inj "on tfr9 run hatvan for $f with e1, becker at 0xff48; ports check"
  assert_contains "$TETHER_STDOUT" "ok" "$f: moving Becker to 0xff48 fixes it"
done
inj "on tfr9 run hatvan for coco2 with e1, becker; ports check"
assert_contains "$TETHER_STDOUT" "ok" "hatvan for coco2 has no clash at default Becker address"

log_test "0xff7f (\$FF7F), core overlaps, and CoCo motherboard reserved ranges"
inj "on tfr9 run nitros9l1 for flat65280 with becker at 0xff7f; ports check"
assert_exit_code 1 "$TETHER_EXIT" "0xff7f is refused"
assert_contains "$(all_out)" "Multi-Pak" "message names Multi-Pak"
inj "on tfr9 run nitros9l2 for deep65280 with emudsk at 0xff90; ports check"
assert_exit_code 1 "$TETHER_EXIT" "emudsk onto gime is refused"
assert_contains "$(all_out)" "gime" "message names gime"
inj "on coco2 run nitros9l2 for coco3 with becker at 0xff14; ports check"
assert_exit_code 1 "$TETHER_EXIT" "coco2 PIA collision is refused"
assert_contains "$(all_out)" "coco_pia" "message names coco_pia"
inj "on coco3 run nitros9l2 for coco3 with rtc at 0xff90; ports check"
assert_exit_code 1 "$TETHER_EXIT" "real coco3 GIME collision is refused"
assert_contains "$(all_out)" "coco3_gime" "message names coco3_gime"
inj "on tfr9 run nitros9l1 for flat65280 with emudsk at 0xff20; ports check"
assert_contains "$TETHER_STDOUT" "ok" "movable core device"
inj "on tfr9 run nitros9l1 for flat65280 with emudsk off; ports check"
assert_exit_code 1 "$TETHER_EXIT" "core device cannot be turned off"
inj "on tfr9 run nitros9l1 for flat65280 with vectors at 0xffe0; ports check"
assert_exit_code 1 "$TETHER_EXIT" "vectors cannot be moved"
inj "on tfr9 run fuzix for multicomp09 with acia at 0xff20; ports check"
assert_exit_code 1 "$TETHER_EXIT" "multicomp09 ports cannot be moved"

log_test "bye refuses conflicts, wrong host, and obsolete Config(engine)"
inj "on tfr9 run hatvan for gep9big with e1, becker; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses conflict"
assert_contains "$(all_out)" "embiggen_mmu" "bye names embiggen_mmu"

inj "on coco2 run nitros9l2 for coco3; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses coco2 target on tfr9 hardware"
assert_contains "$(all_out)" "this hardware is" "bye names host mismatch"
assert_contains "$(all_out)" "tfr9" "bye names tfr9 hardware"

inj "on tfr9 run turbo9os for turbo9sim; set Config(engine) deep65280; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses obsolete Config(engine)"
assert_contains "$(all_out)" "Config(engine) is no longer supported" "bye explains Config(engine) removal"
inj "unset Config(engine); expr 6*7"
assert_contains "$TETHER_STDOUT" "42" "board stayed at TCL> through all refused byes"
run_tether --quick-ping=9
assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=9)" "board did not restart"

inj "on tfr9 run turbo9os for turbo9sim"                      # leave board tidy
print_summary "Suite 7 (ports)"
