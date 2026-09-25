#!/bin/bash
# 06_debug_packets_test.sh - Test --debug=u, --exit, and error handling
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 6: USB Protocol, Diagnostics, and Error Handling"

# 1. Test Immediate Exit Flag (--exit)
log_test "Immediate exit flag (--exit)"
run_tether --exit
assert_exit_code 0 $TETHER_EXIT "Tether --exit returns 0 immediately"
assert_not_contains "$TETHER_STDERR" "Connecting to" "Did not attempt serial connection"

# 2. Test USB COBS Packet Debug Logging (--debug=u)
log_test "USB COBS packet logging (--debug=u)"
run_tether -n --debug=u --stop=c:20 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "PACKET OUT: cmd=181(T_PICO_RPC)" "Logged outgoing RPC packet"
assert_contains "$TETHER_STDERR" "PACKET IN: cmd=181" "Logged incoming RPC response"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 3. Test Invalid Syntax in --stop Suffix
log_test "Invalid suffix in --stop flag"
run_tether -n --stop=c:50Z "$DATA_DIR/bootable/pi.decb" || true
assert_contains "$TETHER_STDERR" "invalid" "Reported invalid cycle count format"
if [ "$TETHER_EXIT" -ne 0 ]; then
    pass "Tether failed with non-zero exit code on bad flag"
else
    fail "Tether unexpectedly succeeded on bad flag"
fi

# 4. Test Non-Existent OS-9 Module Symbol in --watch
log_test "Non-existent module symbol in --watch flag"
run_tether -n --watch=@nonexistentmodule "$BUILD_DIR/turbos/turbos_core.img" || true
assert_contains "$TETHER_STDERR" "not found" "Reported module not found"
if [ "$TETHER_EXIT" -ne 0 ]; then
    pass "Tether failed with non-zero exit code on missing module"
else
    fail "Tether unexpectedly succeeded on missing module"
fi

print_summary "Suite 6 (Diagnostics & Errors)"
