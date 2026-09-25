#!/bin/bash
# 04_trace_flags_test.sh - Test --trace flags (x, +, r, w, i, 1) and listing annotations
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 4: Trace Flags & Disassembly Tests (--trace)"

# 1. Test Opcode Fetch Trace Only (--trace=x)
log_test "Trace opcode fetches only (--trace=x)"
run_tether -n --trace=x --stop=c:50 "$DATA_DIR/bootable/pi.decb"
assert_matches_regex "$TETHER_STDERR" "^x [0-9A-Fa-f]{4} " "Emitted opcode fetch (x) records"
assert_not_contains "$TETHER_STDERR" "+ " "No continuation (+) records emitted"
assert_not_contains "$TETHER_STDERR" "w " "No write (w) records emitted"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 2. Test Opcode Continuation Trace Only (--trace=+)
log_test "Trace opcode continuation only (--trace=+)"
run_tether -n --trace=+ --stop=c:50 "$DATA_DIR/bootable/pi.decb"
assert_matches_regex "$TETHER_STDERR" "^\+ [0-9A-Fa-f]{4} " "Emitted continuation (+) records"
assert_not_contains "$TETHER_STDERR" "x 1000 " "No opcode fetch (x) records emitted"
assert_not_contains "$TETHER_STDERR" "w " "No write (w) records emitted"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 3. Test Write Cycles Only (--trace=w)
log_test "Trace write cycles only (--trace=w)"
run_tether -n --trace=w --stop=c:100 "$DATA_DIR/bootable/pi.decb"
assert_matches_regex "$TETHER_STDERR" "^w [0-9A-Fa-f]{4} " "Emitted write (w) records"
assert_not_contains "$TETHER_STDERR" "x 1000 " "No opcode fetch (x) records emitted"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 4. Test All Trace Flags (--trace=1)
log_test "Trace all bus cycles (--trace=1)"
run_tether -n --trace=1 --stop=c:100 "$DATA_DIR/bootable/pi.decb"
assert_matches_regex "$TETHER_STDERR" "^x [0-9A-Fa-f]{4} " "Emitted opcode fetch (x) records"
assert_matches_regex "$TETHER_STDERR" "^\+ [0-9A-Fa-f]{4} " "Emitted continuation (+) records"
assert_matches_regex "$TETHER_STDERR" "^w [0-9A-Fa-f]{4} " "Emitted write (w) records"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 5. Test Listing Disassembly Cross-Reference
log_test "Disassembly source listing cross-reference"
run_tether -n --trace=x --stop=c:50 "$DATA_DIR/bootable/pi.decb" "$DATA_DIR/bootable/pi.decb.list"
assert_contains "$TETHER_STDERR" "START: ldb #N" "Annotated source line from .list displayed"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 6. Test Primordial Module Symbolic Offset Annotation
log_test "Primordial module symbolic annotation (@module+offset)"
run_tether -n --trace=x --stop=c:20 "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "\"kernel.0b81d8162b\"+001b" "Annotated kernel module name and offset"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

print_summary "Suite 4 (Trace Flags)"
