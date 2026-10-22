#!/bin/bash
# 02_tcl_vfs_test.sh - Test Phase 1 Tcl REPL, VFS (/pc & LittleFS), and line redraw
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 2: Phase 1 Tcl Shell, VFS (/pc & LittleFS), and Console Redraw"

# Ensure board is in Phase 1 (TCL mode)
run_tether --quick-restart=0 >/dev/null

# 1. Test Tcl Expression Evaluation via --quick-inject
log_test "Tcl expression evaluation (--quick-inject='expr 6*7')"
run_tether --quick-inject="expr 6*7"
assert_exit_code 0 $TETHER_EXIT "Quick-inject succeeded"
assert_contains "$TETHER_STDOUT" "42" "Evaluated 6*7 = 42"

# 2. Test Tcl Error Reporting via --quick-inject
log_test "Tcl syntax error handling (--quick-inject='expr 6 * 7')"
run_tether --quick-inject="expr 6 * 7" || true
assert_contains "$TETHER_STDERR" "QUICK-INJECT ERROR: STATUS=1" "Reported Tcl error status"
if [ "$TETHER_EXIT" -ne 0 ]; then
    pass "Quick-inject returned non-zero on Tcl error"
else
    fail "Quick-inject unexpectedly returned 0 on Tcl error"
fi

# 3. Test Tcl FlashLabel Array Population
log_test "Tcl global Label array populated from flash"
run_tether --quick-inject="set Label(p)"
assert_exit_code 0 $TETHER_EXIT "Read Label(p) via Tcl"
assert_contains "$TETHER_STDOUT" "1" "Label(p) == 1"

# 4. Test Host VFS (/pc) File Read and MD5 Checksum from Tcl
log_test "Host VFS (/pc) file access and md5 from Tcl"
TMP_PC=$(mktemp -d /tmp/systest_pc_XXXXXX)
echo -n "Hello from TFR911h VFS systest!" > "$TMP_PC/greeting.txt"
EXPECTED_MD5=$(md5sum "$TMP_PC/greeting.txt" | awk '{print $1}')

run_tether -pc "$TMP_PC" --quick-inject="cat /pc/greeting.txt"
assert_exit_code 0 $TETHER_EXIT "Tcl 'cat /pc/greeting.txt' succeeded"
assert_contains "$TETHER_STDOUT" "Hello from TFR911h VFS systest!" "Read exact file content over VFS RPC"

run_tether -pc "$TMP_PC" --quick-inject="md5 /pc/greeting.txt"
assert_exit_code 0 $TETHER_EXIT "Tcl 'md5 /pc/greeting.txt' succeeded"
assert_contains "$TETHER_STDOUT" "$EXPECTED_MD5" "Firmware MD5 matched host MD5 ($EXPECTED_MD5)"

# 5. Test --quick-upload (target=source) to LittleFS and readback
log_test "LittleFS file upload (--quick-upload) and readback"
run_tether --quick-upload="/tmp/systest.txt=$TMP_PC/greeting.txt"
assert_exit_code 0 $TETHER_EXIT "Uploaded file to LittleFS /tmp/systest.txt"

run_tether --quick-inject="cat /tmp/systest.txt"
assert_exit_code 0 $TETHER_EXIT "Read back /tmp/systest.txt from LittleFS"
assert_contains "$TETHER_STDOUT" "Hello from TFR911h VFS systest!" "LittleFS file content verified"

run_tether --quick-inject="rm /tmp/systest.txt"
assert_exit_code 0 $TETHER_EXIT "Removed temporary file from LittleFS"
rm -rf "$TMP_PC"

# 6. Test TCL Line Redraw Uses Carriage Return (0x0d) Not Line Feed (0x0a)
log_test "TCL interactive line redraw emits CR (0x0d) instead of LF (0x0a)"
HEX_OUT=$(printf "ls\n" | "$TETHER_BIN" -bind="" -pc "$PC_DIR" -cooked -cooked-drain 1s 2>/dev/null | xxd -p | tr -d '\n')
# 0d54434c3e20 = "\rTCL> "
if echo "$HEX_OUT" | grep -q "0d54434c3e206c"; then
    pass "Found '\\rTCL> l' (0d 54 43 4c 3e 20 6c) in console output stream"
else
    fail "Did not find '\\rTCL> l' (0d54434c3e206c) in hex stream: $HEX_OUT"
fi

print_summary "Suite 2 (Tcl REPL & VFS)"
