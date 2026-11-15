#!/bin/sh
# 07_ports_test.sh -- choosing the I/O port placement from Tcl: `ports`, `enhance`, and a `bye` that refuses a bad
# placement (spec 15.x).  Everything is checked at the TCL> prompt; nothing is launched except the final ping.
. "$(dirname "$0")/common.sh"
echo "Running Suite 7: port profiles, enhancements and conflict reporting"
if ! board_present; then skip "whole suite (no board attached)"; print_summary "Suite 7 (ports)"; exit $?; fi

inj() { run_tether --quick-inject="$1"; }                      # sets TETHER_EXIT / TETHER_STDOUT
engine() { inj "ports clear; set Config(engine) $1; menu store Config"; }
all_out() { printf '%s\n%s\n' "$TETHER_STDOUT" "$TETHER_STDERR"; }

run_tether --quick-restart=0 > /dev/null            # a clean boot: the settings are RAM only

log_test "profile and key-value basics"
inj "ports clear; ports profile"
assert_contains "$TETHER_STDOUT" "TFR4" "default profile is TFR4"
inj "ports profile COCO; ports profile"
assert_contains "$TETHER_STDOUT" "COCO" "ports profile COCO sets the profile"
inj "ports COCO"
assert_exit_code 1 "$TETHER_EXIT" "the old shortcut ports COCO is gone: ports is an ensemble"
inj "ports show"
assert_contains "$TETHER_STDOUT" "profile COCO" "ports show lists flat key value pairs"
inj "ports profile Bogus"
assert_exit_code 1 "$TETHER_EXIT" "an unknown profile is an error"
inj "ports set becker banana"
assert_exit_code 1 "$TETHER_EXIT" "a value that is not an address is an error"
inj "ports clear; enhance becker 0xFF48; ports show"
assert_contains "$TETHER_STDOUT" "becker 0xFF48" "enhance becker 0xFF48 shows as a plain key value pair"

log_test "enhancements are listed"
inj "enhance"
assert_contains "$TETHER_STDOUT" "becker 0xFF41-0xFF42" "the list gives each enhancement's default range"
inj "enhance nonesuch"
assert_exit_code 1 "$TETHER_EXIT" "an unknown enhancement is an error"

log_test "the default placement is valid for each engine"
for e in turbo9os flat65280 deep65280 gep9 gep9big multicomp09fuzix; do
  engine $e; inj "ports check"
  assert_contains "$TETHER_STDOUT" "ok" "$e on TFR4"
done

log_test "the known clash: gep9 and gep9big embiggen_mmu vs Becker's default address (spec 15.x)"
for e in gep9 gep9big; do
  engine $e; inj "enhance becker; ports check"
  assert_exit_code 1 "$TETHER_EXIT" "$e: ports check refuses default Becker on TFR4"
  assert_contains "$(all_out)" "becker" "$e: the message names becker"
  assert_contains "$(all_out)" "embiggen_mmu" "$e: the message names embiggen_mmu"
  inj "enhance becker 0xFF48; ports check"
  assert_contains "$TETHER_STDOUT" "ok" "$e: moving Becker to 0xFF48 fixes it"
  inj "ports clear; ports profile COCO; enhance becker; ports check"
  assert_contains "$TETHER_STDOUT" "ok" "$e: the COCO profile has no clash at the default Becker address"
done

log_test "bye refuses a bad placement and leaves the board at TCL>"
engine gep9big; inj "enhance becker; bye"
assert_exit_code 1 "$TETHER_EXIT" "bye failed"
assert_contains "$(all_out)" "embiggen_mmu" "bye's message names the other device"
inj "expr 6*7"
assert_contains "$TETHER_STDOUT" "42" "still at the Tcl prompt, nothing was launched"
run_tether --quick-ping=7
assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=7)" "the board did not restart"

log_test "\$FF7F and core overlaps"
engine flat65280; inj "ports set becker 0xFF7F; ports check"
assert_exit_code 1 "$TETHER_EXIT" "\$FF7F is refused"
assert_contains "$(all_out)" "Multi-Pak" "the message says why"
engine deep65280; inj "ports set emudsk 0xFF90; ports check"
assert_exit_code 1 "$TETHER_EXIT" "emudsk onto the GIME registers is refused"
assert_contains "$(all_out)" "gime" "the message names gime"
engine flat65280; inj "ports set emudsk 0xFF20; ports check"
assert_contains "$TETHER_STDOUT" "ok" "a core device can be moved to free space"
engine flat65280; inj "ports set emudsk off; ports check"
assert_exit_code 1 "$TETHER_EXIT" "a core device cannot be turned off"
engine flat65280; inj "ports set vectors 0xFFE0; ports check"
assert_exit_code 1 "$TETHER_EXIT" "the vectors cannot be moved"

log_test "profiles an engine does not have"
engine multicomp09fuzix; inj "ports profile COCO; ports check"
assert_exit_code 1 "$TETHER_EXIT" "multicomp09fuzix has no COCO profile"
engine gep9; inj "ports profile COCO; ports check"
assert_contains "$TETHER_STDOUT" "ok" "gep9 on COCO"

log_test "engine names: canonical names only; all old aliases are refused"
for alias in multicomp09 flat deep deep64280 hatvan gep9+ big6809 hatvanbig; do
  engine "$alias"; inj "ports check"
  assert_exit_code 1 "$TETHER_EXIT" "removed alias $alias is refused"
done
assert_contains "$(all_out)" "unknown engine" "the message says unknown engine"
assert_contains "$(all_out)" "gep9big" "and lists the canonical names"
engine flat; inj "bye"
assert_exit_code 1 "$TETHER_EXIT" "bye refuses an old alias instead of running Turbo9OS"
run_tether --quick-ping=8
assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=8)" "the board did not restart"
engine multicomp09fuzix; inj "ports check"
assert_contains "$TETHER_STDOUT" "ok" "multicomp09fuzix is accepted"

engine turbo9os                                               # leave the board tidy
print_summary "Suite 7 (ports)"
