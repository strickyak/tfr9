#!/bin/sh
# 04_heap_test.sh -- a script that eats the heap gets a Tcl error, not a reboot (spec S49).  New in v5.
# v4: ckalloc ran out of memory, called panic(), and the board rebooted (USB re-enumerated).
. "$(dirname "$0")/common.sh"
echo "Running Suite 4: heap exhaustion is a Tcl error (S49)"
if ! board_present; then
  skip "whole suite (no board attached)"; print_summary "Suite 4 (heap exhaustion)"; exit $?
fi

# The number is the last digits on a line; the REPL prompt ("TCL> ") can share that line.
log_test "heap-free reports a plausible number"
run_tether --quick-inject="centipede heap-free"
assert_exit_code 0 "$TETHER_EXIT" "centipede heap-free succeeded"
FREE0=$(printf '%s\n' "$TETHER_STDOUT" | grep -o '[0-9][0-9]*[[:space:]]*$' | tail -1 | tr -d '[:space:]')
if [ -n "$FREE0" ] && [ "$FREE0" -gt 20000 ]; then pass "free heap before: $FREE0 bytes"; else fail "bad heap-free output: $TETHER_STDOUT"; fi

log_test "a runaway script gets 'out of memory' as a catchable Tcl error"
run_tether --quick-inject="proc eat {} { for {set i 0} {1} {incr i} { set v\$i \$i } }; catch {eat} msg; set msg"
assert_exit_code 0 "$TETHER_EXIT" "inject returned normally (no reboot, no timeout)"
assert_contains "$TETHER_STDOUT" "out of memory" "script was stopped with an out-of-memory error"

log_test "the board is still the same running firmware and has its memory back"
run_tether --quick-ping=77
assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=77)" "ping after exhaustion"
run_tether --quick-inject="centipede heap-free"
FREE1=$(printf '%s\n' "$TETHER_STDOUT" | grep -o '[0-9][0-9]*[[:space:]]*$' | tail -1 | tr -d '[:space:]')
[ -n "$FREE1" ] || printf "  (heap-free output: %s / %s)\n" "$TETHER_STDOUT" "$TETHER_STDERR"
if [ -n "$FREE1" ] && [ "$FREE1" -gt $((FREE0 - 4096)) ]; then pass "free heap after: $FREE1 bytes (before $FREE0)"; else fail "heap not returned: before=$FREE0 after=$FREE1"; fi

log_test "an uncaught out-of-memory error reports an error status"
run_tether --quick-inject="proc eat {} { for {set i 0} {1} {incr i} { set v\$i \$i } }; eat"
assert_exit_code 1 "$TETHER_EXIT" "uncaught error gives nonzero exit"
assert_contains "$TETHER_STDOUT$TETHER_STDERR" "out of memory" "message mentions out of memory"
run_tether --quick-ping=78
assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=78)" "ping after second exhaustion"

print_summary "Suite 4 (heap exhaustion)"
