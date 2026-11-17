#!/bin/sh
# 03_source_stream_test.sh -- the streaming `source` (spec S38) on real hardware.  New in v5.
# v4's source read the whole script into one ckalloc buffer; this runs scripts far larger than that could be.
. "$(dirname "$0")/common.sh"
echo "Running Suite 3: streaming 'source' (S38)"
if ! board_present; then
  skip "whole suite (no board attached)"; print_summary "Suite 3 (streaming source)"; exit $?
fi
TMP_PC=$(mktemp -d "${TMPDIR:-/tmp}/systest_pc.XXXXXX")
trap 'rm -rf "$TMP_PC"' EXIT INT TERM
run_tether --just-restart=0 >/dev/null

log_test "source a small script from /pc"
printf 'set v1 11\nset v2 22\nexpr {$v1 + $v2}\n' > "$TMP_PC/small.tcl"
run_tether -pc "$TMP_PC" --just-inject="source /pc/small.tcl; set v2"
assert_exit_code 0 "$TETHER_EXIT" "source succeeded"
assert_contains "$TETHER_STDOUT" "22" "variables set by the sourced script are visible"

log_test "source stops at the first error and reports it"
printf 'set before 1\nerror boom\nset after 1\n' > "$TMP_PC/err.tcl"
run_tether -pc "$TMP_PC" --just-inject="source /pc/err.tcl"
if [ "$TETHER_EXIT" -ne 0 ]; then pass "error propagated"; else fail "error did not propagate"; fi
assert_contains "$TETHER_STDERR" "boom" "error message reported"
run_tether --just-inject="info exists after"
assert_contains "$TETHER_STDOUT" "0" "commands after the error did not run"

log_test "nested source and multi-line commands"
printf 'proc twice {n} {\n  expr {$n * 2}\n}\nset b [twice 21]\n' > "$TMP_PC/b.tcl"
printf 'source /pc/b.tcl\nset a $b\n' > "$TMP_PC/a.tcl"
run_tether -pc "$TMP_PC" --just-inject="source /pc/a.tcl; set a"
assert_contains "$TETHER_STDOUT" "42" "nested source and proc definition worked"

log_test "a ~600 KB script from /pc (v4 would try to ckalloc it all at once)"
awk 'BEGIN { for (i = 1; i <= 30000; i++) print "set v " i; print "set done 1" }' > "$TMP_PC/big.tcl"
echo "  script size: $(size_of "$TMP_PC/big.tcl") bytes"
run_tether -pc "$TMP_PC" --just-inject="source /pc/big.tcl; list \$done \$v"
assert_exit_code 0 "$TETHER_EXIT" "big script sourced"
assert_contains "$TETHER_STDOUT" "1 30000" "last command of the big script ran"

log_test "the board still answers after the big script"
run_tether --just-ping=123
assert_contains "$TETHER_STDOUT" "just-ping: OK (value=123)" "ping after big script"

print_summary "Suite 3 (streaming source)"
