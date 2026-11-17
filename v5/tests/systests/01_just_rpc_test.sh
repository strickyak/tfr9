#!/bin/sh
# 01_just_rpc_test.sh -- just-* CLI flags, UF2/RPC label management, and PicoRPC.  (Ported from v4; POSIX sh.)
. "$(dirname "$0")/common.sh"
echo "Running Suite 1: Just CLI Flags, Label Management, and PicoRPC"

log_test "Immediate exit flag (--exit)"
run_tether --exit
assert_exit_code 0 "$TETHER_EXIT" "Tether --exit returns 0 immediately"
assert_not_contains "$TETHER_STDERR" "UsbOpen" "Did not open serial connection"

log_test "Removed legacy --quick-* flags are rejected"
run_tether --quick-ping=1
if [ "$TETHER_EXIT" -ne 0 ]; then pass "Rejected removed --quick-ping flag"
else fail "Unexpectedly accepted removed --quick-ping flag"; fi

log_test "Offline UF2 label generation (--just-label-data)"
TMP_UF2=$(mktemp "${TMPDIR:-/tmp}/test_label.XXXXXX")
LABEL_STR="p=1,b=tfr9,v=911h,mhz=250,id=systest"
run_tether --just-label-data="$LABEL_STR" --just-label-filename="$TMP_UF2"
assert_exit_code 0 "$TETHER_EXIT" "MakeUf2Label exited cleanly"
assert_contains "$TETHER_STDOUT" "Target Address: 0x10FFF000" "UF2 target address is 0x10FFF000"
if [ -f "$TMP_UF2" ] && [ "$(size_of "$TMP_UF2")" -eq 512 ]; then pass "Generated UF2 file is 512 bytes"
else fail "Generated UF2 file missing or not 512 bytes"; fi
rm -f "$TMP_UF2"

log_test "Reject UF2 label not starting with 'p=1,'"
TMP_UF2=$(mktemp "${TMPDIR:-/tmp}/test_bad_label.XXXXXX")
run_tether --just-label-data="b=tfr9,v=911h" --just-label-filename="$TMP_UF2"
assert_contains "$TETHER_STDERR" "Label must begin with" "Reported missing p=1, prefix"
if [ "$TETHER_EXIT" -ne 0 ]; then pass "Rejected invalid label with non-zero exit code"; else fail "Unexpectedly succeeded on invalid label prefix"; fi
rm -f "$TMP_UF2"

log_test "PicoRPC board restart into mode 0 (--just-restart=0)"
if need_board "restart RPC"; then
  run_tether --just-restart=0
  assert_exit_code 0 "$TETHER_EXIT" "Tether --just-restart=0 exited 0"
  assert_contains "$TETHER_STDOUT" "just-restart: OK" "Pico acknowledged restart RPC"
fi

log_test "PicoRPC ping round-trip (--just-ping=456789)"
if need_board "ping RPC"; then
  run_tether --just-ping=456789
  assert_exit_code 0 "$TETHER_EXIT" "Tether --just-ping exited 0"
  assert_contains "$TETHER_STDOUT" "just-ping: OK (value=456789)" "Echoed 32-bit ping payload"
fi

log_test "PicoRPC read & Phase-1 set flash label (--just-get-label / --just-set-label)"
if need_board "get-label / set-label RPC"; then
  run_tether --just-get-label
  assert_exit_code 0 "$TETHER_EXIT" "Tether --just-get-label exited 0"
  assert_matches_regex "$TETHER_STDOUT" "^p=1," "Flash label begins with 'p=1,'"
  ORIG_LABEL=$(printf '%s\n' "$TETHER_STDOUT" | head -1)

  run_tether --just-restart=27 >/dev/null
  run_tether --just-set-label="$ORIG_LABEL"
  assert_exit_code 0 "$TETHER_EXIT" "Tether --just-set-label round-trip exited 0"
  assert_contains "$TETHER_STDOUT" "just-set-label: OK" "Pico confirmed label write"
  run_tether --just-get-label
  GOT_LABEL=$(printf '%s\n' "$TETHER_STDOUT" | head -1)
  assert_equal "$ORIG_LABEL" "$GOT_LABEL" "Flash label preserved byte-for-byte"
fi

print_summary "Suite 1 (Just RPC & Labels)"
