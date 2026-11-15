#!/bin/sh
# 02_tcl_vfs_test.sh -- Phase 1 Tcl REPL, VFS (/pc and LittleFS), line redraw.  (Ported from v4; POSIX sh.)
# Needs a board.  /pc is a fresh temporary directory (tether never creates it, spec 11.13).
. "$(dirname "$0")/common.sh"
echo "Running Suite 2: Phase 1 Tcl Shell, VFS (/pc & LittleFS), and Console Redraw"
if ! board_present; then
  skip "whole suite (no board attached)"; print_summary "Suite 2 (Tcl REPL & VFS)"; exit $?
fi
TMP_PC=$(mktemp -d "${TMPDIR:-/tmp}/systest_pc.XXXXXX")
trap 'rm -rf "$TMP_PC"' EXIT INT TERM

run_tether --quick-restart=0 >/dev/null     # make sure the board is in Phase 1 (TCL mode)

log_test "Tcl expression evaluation (--quick-inject='expr 6*7')"
run_tether --quick-inject="expr 6*7"
assert_exit_code 0 "$TETHER_EXIT" "Quick-inject succeeded"
assert_contains "$TETHER_STDOUT" "42" "Evaluated 6*7 = 42"

log_test "Tcl syntax error handling (--quick-inject='expr 6 * 7')"
run_tether --quick-inject="expr 6 * 7"
assert_contains "$TETHER_STDERR" "QUICK-INJECT ERROR: STATUS=1" "Reported Tcl error status"
if [ "$TETHER_EXIT" -ne 0 ]; then pass "Quick-inject returned non-zero on Tcl error"; else fail "Quick-inject unexpectedly returned 0 on Tcl error"; fi

log_test "Tcl global Label array populated from flash"
run_tether --quick-inject="set Label(p)"
assert_exit_code 0 "$TETHER_EXIT" "Read Label(p) via Tcl"
assert_contains "$TETHER_STDOUT" "1" "Label(p) == 1"

log_test "Host VFS (/pc) file access and md5 from Tcl"
printf '%s' "Hello from TFR911h VFS systest!" > "$TMP_PC/greeting.txt"
EXPECTED_MD5=$(md5_of "$TMP_PC/greeting.txt")
run_tether -pc "$TMP_PC" --quick-inject="cat /pc/greeting.txt"
assert_exit_code 0 "$TETHER_EXIT" "Tcl 'cat /pc/greeting.txt' succeeded"
assert_contains "$TETHER_STDOUT" "Hello from TFR911h VFS systest!" "Read exact file content over VFS RPC"
run_tether -pc "$TMP_PC" --quick-inject="md5 /pc/greeting.txt"
assert_exit_code 0 "$TETHER_EXIT" "Tcl 'md5 /pc/greeting.txt' succeeded"
assert_contains "$TETHER_STDOUT" "$EXPECTED_MD5" "Firmware MD5 matched host MD5 ($EXPECTED_MD5)"

log_test "/pc refuses symlinks (spec 11.13): a link inside -pc is invisible to the board"
printf 'secret' > "$TMP_PC/../systest_secret.txt" 2>/dev/null
if ln -s "../systest_secret.txt" "$TMP_PC/escape.txt" 2>/dev/null; then
  run_tether -pc "$TMP_PC" --quick-inject="cat /pc/escape.txt"
  if [ "$TETHER_EXIT" -ne 0 ]; then pass "cat of a symlink failed"; else fail "cat of a symlink unexpectedly succeeded"; fi
  assert_not_contains "$TETHER_STDOUT" "secret" "symlink target not readable through /pc"
  rm -f "$TMP_PC/escape.txt"
else skip "cannot create a symlink here"; fi
rm -f "$TMP_PC/../systest_secret.txt"

log_test "/pc is disabled, not auto-created, when -pc points at a missing directory"
MISSING="$TMP_PC/does-not-exist"
run_tether -pc "$MISSING" --quick-inject="cat /pc/greeting.txt"
if [ "$TETHER_EXIT" -ne 0 ]; then pass "cat failed with a missing /pc"; else fail "cat unexpectedly succeeded with a missing /pc"; fi
if [ ! -e "$MISSING" ]; then pass "tether did not create the directory"; else fail "tether created $MISSING"; fi

log_test "LittleFS file upload (--quick-upload) and readback"
run_tether --quick-upload="/tmp/systest.txt=$TMP_PC/greeting.txt"
assert_exit_code 0 "$TETHER_EXIT" "Uploaded file to LittleFS /tmp/systest.txt"
run_tether --quick-inject="cat /tmp/systest.txt"
assert_exit_code 0 "$TETHER_EXIT" "Read back /tmp/systest.txt from LittleFS"
assert_contains "$TETHER_STDOUT" "Hello from TFR911h VFS systest!" "LittleFS file content verified"
run_tether --quick-inject="rm /tmp/systest.txt"
assert_exit_code 0 "$TETHER_EXIT" "Removed temporary file from LittleFS"

log_test "TCL interactive line redraw emits CR (0x0d) instead of LF (0x0a)"
HEX_OUT=$(printf 'ls\n' | "$TETHER" -bind="" ${WIRE:+-wire="$WIRE"} -pc "$TMP_PC" -cooked -cooked-drain 1s 2>/dev/null | od -An -tx1 | tr -d ' \n')
# 0d54434c3e20 = "\rTCL> "
if printf '%s' "$HEX_OUT" | grep -q "0d54434c3e206c"; then pass "Found '\\rTCL> l' in console output stream"
else fail "Did not find '\\rTCL> l' (0d54434c3e206c) in hex stream: $HEX_OUT"; fi

print_summary "Suite 2 (Tcl REPL & VFS)"
