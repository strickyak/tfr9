#!/bin/bash
# 05_watchpoints_test.sh - Test --watch (all, r, w, x, :N, @module, combo with trace)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 5: Logging Watchpoints Tests (--watch)"

# 1. Test Silent Tracing with Single Watchpoint (--watch=0x1000)
log_test "Silent tracing with single watchpoint (--watch=0x1000 without --trace)"
run_tether -n --watch=0x1000 --max=c:200 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "x 1000 C6 #4;" "Watched cycle #4 for \$1000 was emitted"
count=$(echo "$TETHER_STDERR" | grep -c -E '^[x\+rw\-] [0-9A-Fa-f]{4} ' || true)
if [ "$count" -eq 1 ]; then
    pass "Exactly 1 cycle was emitted across 200 execution cycles"
else
    fail "Expected exactly 1 emitted cycle, got $count"
fi
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 2. Test Multiple Watchpoints (--watch=0x1000,0x1002,0x1004)
log_test "Multiple watchpoints (--watch=0x1000,0x1002,0x1004)"
run_tether -n --watch=0x1000,0x1002,0x1004 --max=c:200 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "x 1000 C6 #4;" "Cycle \$1000 was emitted"
assert_contains "$TETHER_STDERR" "x 1002 34 #6;" "Cycle \$1002 was emitted"
assert_contains "$TETHER_STDERR" "x 1004 8D #12;" "Cycle \$1004 was emitted"
count=$(echo "$TETHER_STDERR" | grep -c -E '^[x\+rw\-] [0-9A-Fa-f]{4} ' || true)
if [ "$count" -eq 3 ]; then
    pass "Exactly 3 cycles were emitted"
else
    fail "Expected exactly 3 emitted cycles, got $count"
fi
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 3. Test Watchpoint with Nth Count (--watch=0x1019:3)
log_test "Watchpoint with Nth count (--watch=0x1019:3)"
run_tether -n --watch=0x1019:3 --max=c:120 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "x 1019 ED #69;" "3rd hit at cycle #69 was emitted"
assert_not_contains "$TETHER_STDERR" "#37;" "1st hit at cycle #37 was not emitted"
assert_not_contains "$TETHER_STDERR" "#53;" "2nd hit at cycle #53 was not emitted"
count=$(echo "$TETHER_STDERR" | grep -c -E '^[x\+rw\-] [0-9A-Fa-f]{4} ' || true)
if [ "$count" -eq 1 ]; then
    pass "Exactly 1 cycle was emitted for the 3rd hit"
else
    fail "Expected exactly 1 emitted cycle, got $count"
fi
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 4. Test Symbolic Module Watchpoint (--watch=@kernel+0x1B)
log_test "Symbolic module watchpoint (--watch=@kernel+0x1B)"
run_tether -n --watch=@kernel+0x1B --max=c:50k "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Configured logging watchpoint @kernel+0x1B -> \$E3CC" "Resolved @kernel+0x1B"
assert_contains "$TETHER_STDERR" "x E3CC 4F #11; \"kernel.0b81d8162b\"+001b" "Watched kernel entry cycle emitted with annotations"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 5. Test Pre-Trigger Logging Combined with Trace (--watch=0x1000 --trigger=c:60 --trace=x)
log_test "Pre-trigger watchpoint logging combined with --trace"
run_tether -n --watch=0x1000 --trigger=c:60 --trace=x --max=c:100 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "x 1000 C6 #4;" "Pre-trigger cycle #4 emitted by watchpoint"
assert_contains "$TETHER_STDERR" "x 101B 31 #61;" "Post-trigger cycle #61 emitted by trace"
assert_contains "$TETHER_STDERR" "x 1019 ED #69;" "Post-trigger cycle #69 emitted by trace"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

print_summary "Suite 5 (Watchpoints)"
