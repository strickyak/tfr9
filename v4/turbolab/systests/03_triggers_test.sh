#!/bin/bash
# 03_triggers_test.sh - Test --trigger (cycles, time, watchpoints) and delayed trace
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 3: Tracing Trigger Tests (--trigger)"

# 1. Test Cycle Trigger (Phase 2 suppression and Phase 3 activation)
log_test "Cycle trigger (--trigger=c:60 --max=c:100 --trace=x)"
run_tether -n --trigger=c:60 --max=c:100 --trace=x "$DATA_DIR/bootable/pi.decb"
# Cycle 4 ($1000) occurred before trigger 60 and should NOT be in trace
assert_not_contains "$TETHER_STDERR" "x 1000 C6 #4;" "Pre-trigger cycle #4 was suppressed"
# Cycle 61 ($101B) occurred after trigger 60 and SHOULD be in trace
assert_contains "$TETHER_STDERR" "x 101B 31 #61;" "Post-trigger cycle #61 was captured"
assert_contains "$TETHER_STDERR" "x 1019 ED #69;" "Post-trigger cycle #69 was captured"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 2. Test Watchpoint Trigger with Count (3rd hit of 0x1019)
log_test "Watchpoint trigger with Nth count (--trigger=x:0x1019:3 --max=c:120 --trace=x)"
run_tether -n --trigger=x:0x1019:3 --max=c:120 --trace=x "$DATA_DIR/bootable/pi.decb"
# 1st hit of $1019 was cycle #37; 2nd was #53; both should be suppressed
assert_not_contains "$TETHER_STDERR" "x 1019 ED #37;" "1st hit (cycle #37) was suppressed"
assert_not_contains "$TETHER_STDERR" "x 1019 ED #53;" "2nd hit (cycle #53) was suppressed"
# 3rd hit was cycle #69; should trigger tracing
assert_contains "$TETHER_STDERR" "x 1019 ED #69;" "3rd hit (cycle #69) triggered trace output"
assert_contains "$TETHER_STDERR" "x 101B 31 #77;" "Subsequent instruction (cycle #77) was traced"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 3. Test Symbolic Module Trigger (--trigger=@kernel+0x1B)
log_test "Symbolic module trigger (--trigger=@kernel+0x1B --max=c:30 --trace=x)"
run_tether -n --trigger=@kernel+0x1B --max=c:30 --trace=x "$BUILD_DIR/turbos/turbos_core.img"
# Reset cycles #1..#10 should be suppressed
assert_not_contains "$TETHER_STDERR" "#1;" "Reset vector fetch #1 was suppressed"
assert_not_contains "$TETHER_STDERR" "#4;" "First instruction #4 was suppressed"
# Kernel entry at cycle #11 ($E3CC) should trigger trace
assert_contains "$TETHER_STDERR" "x E3CC 4F #11;" "Kernel entry instruction #11 triggered trace"
assert_contains "$TETHER_STDERR" "x E3CD 5F #13;" "Subsequent instruction #13 was traced"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

print_summary "Suite 3 (Triggers)"
