#!/bin/bash
# 02_stop_limits_test.sh - Test --stop cycle, time, and watchpoint limits + core dump
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "Running Suite 2: Execution Limits Tests (--stop)"

# 1. Test Decimal SI Cycle Limit (c:10k)
log_test "Cycle limit with decimal SI suffix (--stop=c:10k)"
run_tether -n --stop=c:10k "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Max Cycles Limit Reached at Cycle #10000" "Halted exactly at 10,000 cycles"
assert_contains "$TETHER_STDERR" "CPU Registers (SWI Capture, 6809 mode)" "Captured register frame via SWI"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# Verify core dump file
log_test "Verify 64KB core dump file generation"
if [ -f "/tmp/fault.img" ]; then
    size=$(stat -c%s "/tmp/fault.img")
    if [ "$size" -eq 65536 ]; then
        pass "Core dump /tmp/fault.img exists and is exactly 65536 bytes"
    else
        fail "Core dump /tmp/fault.img size is $size (expected 65536)"
    fi
else
    fail "Core dump /tmp/fault.img was not created"
fi

# 2. Test Binary Cycle Limit (c:16K)
log_test "Cycle limit with binary multiplier (--stop=c:16K = 16384 cycles)"
run_tether -n --stop=c:16K "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Max Cycles Limit Reached at Cycle #16384" "Halted exactly at 16,384 cycles"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 3. Test Time Limit (t:300ms)
log_test "Wall-clock duration limit (--stop=t:300ms)"
run_tether -n --stop=t:300ms "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Max Time Limit Reached" "Halted upon wall-clock time limit"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 4. Test Watchpoint Limit (Literal address x:0x1000)
log_test "Watchpoint limit on entry address (--stop=x:0x1000)"
run_tether -n --stop=x:0x1000 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "Watchpoint Limit Reached" "Halted upon reaching watchpoint"
assert_contains "$TETHER_STDERR" "Addr=\$1000" "Watchpoint fault address matched \$1000"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 5. Test Watchpoint Limit with Nth Count (x:0x1019:5)
log_test "Watchpoint limit with Nth count (--stop=x:0x1019:5)"
run_tether -n --stop=x:0x1019:5 "$DATA_DIR/bootable/pi.decb"
assert_contains "$TETHER_STDERR" "Watchpoint Limit Reached at Cycle #101 (Addr=\$1019" "Halted on the 5th execution of \$1019"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 6. Test Watchpoint Limit with Symbolic OS-9 Module Offset (--stop=@kernel+0x1B)
log_test "Watchpoint limit on module symbol (--stop=@kernel+0x1B)"
run_tether -n --stop=@kernel+0x1B "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Resolved stop watchpoint @kernel+0x1B to \$E3CC" "Resolved @kernel+0x1B to \$E3CC"
assert_contains "$TETHER_STDERR" "Watchpoint Limit Reached at Cycle #11 (Addr=\$E3CC" "Halted at kernel entry instruction"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

# 7. Test Deprecated Alias --max Backwards Compatibility (--max=c:5k)
log_test "Deprecated --max alias backwards compatibility (--max=c:5k)"
run_tether -n --max=c:5k "$BUILD_DIR/turbos/turbos_core.img"
assert_contains "$TETHER_STDERR" "Max Cycles Limit Reached at Cycle #5000" "Halted exactly at 5,000 cycles using --max alias"
assert_exit_code 0 $TETHER_EXIT "Tether exited cleanly"

print_summary "Suite 2 (Limits)"
