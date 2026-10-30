#!/bin/sh
# 01_quick_rpc_test.sh -- quick CLI flags, UF2 label generation, and PicoRPC.  (Ported from v4; POSIX sh.)
. "$(dirname "$0")/common.sh"
echo "Running Suite 1: Quick CLI Flags, UF2 Label Generation, and PicoRPC"

log_test "Immediate exit flag (--exit)"
run_tether --exit
assert_exit_code 0 "$TETHER_EXIT" "Tether --exit returns 0 immediately"
assert_not_contains "$TETHER_STDERR" "UsbOpen" "Did not open serial connection"

log_test "Offline UF2 label generation (--quick-label-data)"
TMP_UF2=$(mktemp "${TMPDIR:-/tmp}/test_label.XXXXXX")
LABEL_STR="p=1,b=tfr9,v=911h,mhz=250,id=systest"
run_tether --quick-label-data="$LABEL_STR" --quick-label-filename="$TMP_UF2"
assert_exit_code 0 "$TETHER_EXIT" "MakeUf2Label exited cleanly"
assert_contains "$TETHER_STDOUT" "Target Address: 0x10FFF000" "UF2 target address is 0x10FFF000"
if [ -f "$TMP_UF2" ] && [ "$(size_of "$TMP_UF2")" -eq 512 ]; then pass "Generated UF2 file is 512 bytes"
else fail "Generated UF2 file missing or not 512 bytes"; fi
rm -f "$TMP_UF2"

log_test "Reject UF2 label not starting with 'p=1,'"
TMP_UF2=$(mktemp "${TMPDIR:-/tmp}/test_bad_label.XXXXXX")
run_tether --quick-label-data="b=tfr9,v=911h" --quick-label-filename="$TMP_UF2"
assert_contains "$TETHER_STDERR" "Label must begin with" "Reported missing p=1, prefix"
if [ "$TETHER_EXIT" -ne 0 ]; then pass "Rejected invalid label with non-zero exit code"; else fail "Unexpectedly succeeded on invalid label prefix"; fi
rm -f "$TMP_UF2"

log_test "PicoRPC board restart into mode 0 (--quick-restart=0)"
if need_board "restart RPC"; then
  run_tether --quick-restart=0
  assert_exit_code 0 "$TETHER_EXIT" "Tether --quick-restart=0 exited 0"
  assert_contains "$TETHER_STDOUT" "quick-restart: OK" "Pico acknowledged restart RPC"
fi

log_test "PicoRPC ping round-trip (--quick-ping=456789)"
if need_board "ping RPC"; then
  run_tether --quick-ping=456789
  assert_exit_code 0 "$TETHER_EXIT" "Tether --quick-ping exited 0"
  assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=456789)" "Echoed 32-bit ping payload"
fi

log_test "PicoRPC read flash label (--quick-get-label)"
if need_board "get-label RPC"; then
  run_tether --quick-get-label
  assert_exit_code 0 "$TETHER_EXIT" "Tether --quick-get-label exited 0"
  assert_matches_regex "$TETHER_STDOUT" "^p=1," "Flash label begins with 'p=1,'"
fi

print_summary "Suite 1 (Quick RPC & Labels)"
