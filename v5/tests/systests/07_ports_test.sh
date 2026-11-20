#!/bin/sh
# 07_ports_test.sh -- `on ... run ... for ... with ...`, `ports list|check`, and checked `bye` (spec 7, 15.y).
# Everything is verified at the TCL> prompt on real TFR/911H hardware; nothing launches the 6309.
. "$(dirname "$0")/common.sh"
echo "Running Suite 7: on/run/for/with grammar, ports list|check, and conflict reporting"
if ! board_present; then skip "whole suite (no board attached)"; print_summary "Suite 7 (ports)"; exit $?; fi

inj() { run_tether --just-inject="$1"; }                      # sets TETHER_EXIT / TETHER_STDOUT / TETHER_STDERR
all_out() { printf '%s\n%s\n' "$TETHER_STDOUT" "$TETHER_STDERR"; }

run_tether --just-restart=0 > /dev/null            # clean boot: settings live in RAM only

log_test "default target and minimal ports ensemble (list|check only)"
inj "on"
assert_contains "$TETHER_STDOUT" "tfr911h" "default host is tfr911h"
inj "ports list"
assert_contains "$TETHER_STDOUT" "on tfr911h run turbo9os for turbo9sim engine hamster_flat" "default ports list"
inj "ports check"
assert_contains "$TETHER_STDOUT" "ok" "default ports check is ok"
for gone in "ports" "ports show" "ports profile coco" "ports set becker on" "ports clear" "enhance"; do
  inj "$gone"
  assert_exit_code 1 "$TETHER_EXIT" "removed command '$gone' is an error"
done

log_test "retired 'tfr9' host and ',' separator are cleanly refused"
inj "on tfr9 run turbo9os"
assert_exit_code 1 "$TETHER_EXIT" "retired host 'tfr9' is refused"
assert_contains "$(all_out)" "tfr911h" "error suggests tfr911h"
inj "ON coco2 RUN nitros9l2 FOR coco3 WITH e1, becker"
assert_exit_code 1 "$TETHER_EXIT" "comma separator in WITH is refused"
assert_contains "$(all_out)" "repeat 'with'" "error explains repeated WITH"

log_test "ON ... RUN ... FOR ... WITH ... WITH ... uppercase folding & key-value digestion"
inj "ON coco2 RUN nitros9l2 FOR coco3 WITH E1 WITH becker at 0xFF41 WITH dw over bitbang baud 9600 WITH rtc at 0xFFE0; PORTS LIST"
assert_contains "$TETHER_STDOUT" "on coco2 run nitros9l2 for coco3 engine gerbil_gime" "compiled host/os/target/engine"
assert_contains "$TETHER_STDOUT" "with e1" "compiled enhancement map in lowercase"
assert_contains "$TETHER_STDOUT" "becker on becker.at 0xff41" "compiled becker and 0x... address in lowercase"
assert_contains "$TETHER_STDOUT" "dw on dw.over bitbang dw.baud 9600" "compiled multi-parameter dw transport"
assert_contains "$TETHER_STDOUT" "rtc on rtc.at 0xffe0" "compiled rtc override"
inj "PORTS CHECK"
assert_contains "$TETHER_STDOUT" "ok" "coco2 nitros9l2 for coco3 validates"

log_test "ON tfr911h RUN turbolab WITH trigger = r/0xFFE8 WITH trace_count = 10000"
inj "ON tfr911h RUN turbolab WITH trigger = r/0xFFE8 WITH trace_count = 10000; PORTS LIST"
assert_contains "$TETHER_STDOUT" "on tfr911h run turbolab for turbo9sim engine hamster_turbolab" "compiled turbolab target & engine"
assert_contains "$TETHER_STDOUT" "trigger r/0xFFE8 trace_count 10000" "compiled turbolab trigger & trace_count"
inj "PORTS CHECK"
assert_contains "$TETHER_STDOUT" "ok" "turbolab configuration validates"

log_test "coco2 vs coco3 engine selection and 'on <host>' + 'run ...' shorthand"
inj "on coco3; run nitros9l2 for coco3 with e1 with becker; ports list"
assert_contains "$TETHER_STDOUT" "on coco3 run nitros9l2 for coco3 engine gerbil_io" "real coco3 implies gerbil_io"
inj "ports check"
assert_contains "$TETHER_STDOUT" "ok" "coco3 nitros9l2 for coco3 validates"

log_test "every tfr911h target validates"
for pair in "turbo9os turbo9sim" "turbolab turbo9sim" "nitros9l1 flat65280" "nitros9l2 deep65280" \
            "nitros9l2 jamesbrown" "nitros9l2 coco3" \
            "hatvan gep9" "hatvan coco2" "fuzix multicomp09" "rsbasic coco2"; do
  set -- $pair
  inj "on tfr911h run $1 for $2; ports check"
  assert_contains "$TETHER_STDOUT" "ok" "tfr911h: $1 for $2"
done
inj "on tfr911h run rsbasic for coco2 with e1 with info with rtc with becker; ports check"
assert_contains "$TETHER_STDOUT" "ok" "tfr911h: rsbasic for coco2 + e1 enhancements"
inj "on tfr911h run hatvan for gep9big; ports check"
assert_exit_code 1 "$TETHER_EXIT" "retired 'gep9big' target is refused"

log_test "the known clash: gep9 embiggen_mmu vs Becker's default address (spec 15.x)"
inj "on tfr911h run hatvan for gep9 with e1 with becker; ports check"
assert_exit_code 1 "$TETHER_EXIT" "gep9: ports check refuses default Becker"
assert_contains "$(all_out)" "becker" "gep9: message names becker"
assert_contains "$(all_out)" "embiggen_mmu" "gep9: message names embiggen_mmu"
inj "on tfr911h run hatvan for gep9 with e1 with becker at 0xff48; ports check"
assert_contains "$TETHER_STDOUT" "ok" "gep9: moving Becker to 0xff48 fixes it"
inj "on tfr911h run hatvan for coco2 with e1 with becker; ports check"
assert_contains "$TETHER_STDOUT" "ok" "hatvan for coco2 has no clash at default Becker address"

log_test "0xff7f (\$FF7F), core overlaps, and CoCo motherboard reserved ranges"
inj "on tfr911h run nitros9l1 for flat65280 with becker at 0xff7f; ports check"
assert_exit_code 1 "$TETHER_EXIT" "0xff7f is refused"
assert_contains "$(all_out)" "Multi-Pak" "message names Multi-Pak"
inj "on tfr911h run nitros9l2 for deep65280 with emudsk at 0xff90; ports check"
assert_exit_code 1 "$TETHER_EXIT" "emudsk onto gime is refused"
assert_contains "$(all_out)" "gime" "message names gime"
inj "on coco2 run nitros9l2 for coco3 with becker at 0xff14; ports check"
assert_exit_code 1 "$TETHER_EXIT" "coco2 PIA collision is refused"
assert_contains "$(all_out)" "coco_pia" "message names coco_pia"
inj "on coco3 run nitros9l2 for coco3 with rtc at 0xff90; ports check"
assert_exit_code 1 "$TETHER_EXIT" "real coco3 GIME collision is refused"
assert_contains "$(all_out)" "coco3_gime" "message names coco3_gime"
inj "on tfr911h run nitros9l1 for flat65280 with emudsk at 0xff20; ports check"
assert_contains "$TETHER_STDOUT" "ok" "movable core device"
inj "on tfr911h run nitros9l1 for flat65280 with emudsk off; ports check"
assert_exit_code 1 "$TETHER_EXIT" "core device cannot be turned off"
inj "on tfr911h run nitros9l1 for flat65280 with vectors at 0xffe0; ports check"
assert_exit_code 1 "$TETHER_EXIT" "vectors cannot be moved"
inj "on tfr911h run fuzix for multicomp09 with acia at 0xff20; ports check"
assert_exit_code 1 "$TETHER_EXIT" "multicomp09 ports cannot be moved"

log_test "bye refuses conflicts, wrong host, missing turbolab image, and obsolete Config(engine)"
inj "on tfr911h run hatvan for gep9 with e1 with becker; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses conflict"
assert_contains "$(all_out)" "embiggen_mmu" "bye names embiggen_mmu"

inj "on tfr911h run turbolab with image = /lfs/no_such_file_9999.decb; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses missing turbolab image"
assert_contains "$(all_out)" "cannot open image" "bye names missing turbolab image"

inj "on coco2 run nitros9l2 for coco3; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses coco2 target on tfr911h hardware"
assert_contains "$(all_out)" "this hardware is" "bye names host mismatch"
assert_contains "$(all_out)" "tfr911h" "bye names tfr911h hardware"

inj "on tfr911h run turbo9os for turbo9sim; set Config(engine) deep65280; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses obsolete Config(engine)"
assert_contains "$(all_out)" "Config(engine) is no longer supported" "bye explains Config(engine) removal"
inj "unset Config(engine); expr 6*7"
assert_contains "$TETHER_STDOUT" "42" "board stayed at TCL> through all refused byes"
run_tether --just-ping=9
assert_contains "$TETHER_STDOUT" "just-ping: OK (value=9)" "board did not restart"

inj "on tfr911h run turbo9os for turbo9sim"                    # leave board tidy
print_summary "Suite 7 (ports)"
