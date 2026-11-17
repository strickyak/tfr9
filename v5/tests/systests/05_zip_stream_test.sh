#!/bin/sh
# 05_zip_stream_test.sh -- zip entries are streamed, never extracted whole to the heap (spec S38).  New in v5.
# v4 extracted the entire entry to the heap on open, so a 200 KB entry (the board has ~68 KB of heap) could
# not be read at all.  Needs the `zip` command on the PC.
. "$(dirname "$0")/common.sh"
echo "Running Suite 5: streamed zip/.pak entries (S38)"
if ! board_present; then
  skip "whole suite (no board attached)"; print_summary "Suite 5 (zip streaming)"; exit $?
fi
if ! command -v zip > /dev/null 2>&1; then
  skip "whole suite (no zip command on this PC)"; print_summary "Suite 5 (zip streaming)"; exit $?
fi
TMP_PC=$(mktemp -d "${TMPDIR:-/tmp}/systest_pc.XXXXXX")
trap 'rm -rf "$TMP_PC"' EXIT INT TERM
# Start from a freshly booted board: a deflated entry needs ~47 KB of heap, which a fragmented heap cannot give.
run_tether --just-restart=0 > /dev/null

# A STORED 200 KB entry of incompressible-looking bytes, a DEFLATED 100 KB text entry, and a small one.
mkdir "$TMP_PC/src"
awk 'BEGIN { srand(7); for (i = 0; i < 200000; i++) printf "%c", int(rand() * 255) + 1 }' > "$TMP_PC/src/big.bin"
awk 'BEGIN { for (i = 1; i <= 4000; i++) printf "line %d of the deflated entry, quite compressible\n", i }' > "$TMP_PC/src/text.txt"
printf 'hello from a zip\n' > "$TMP_PC/src/small.txt"
( cd "$TMP_PC/src" && zip -q -0 ../pack.zip big.bin && zip -q -9 ../pack.zip text.txt small.txt )
echo "  big.bin $(size_of "$TMP_PC/src/big.bin") bytes stored; text.txt $(size_of "$TMP_PC/src/text.txt") bytes deflated"

mdnum() { printf '%s\n' "$1" | grep -o '[0-9a-f]\{32\}' | tail -1; }
heapnum() { printf '%s\n' "$1" | grep -o '[0-9][0-9]*[[:space:]]*$' | tail -1 | tr -d '[:space:]'; }

run_tether -pc "$TMP_PC" --just-inject="centipede heap-free"
FREE0=$(heapnum "$TETHER_STDOUT")

log_test "small deflated entry"
run_tether -pc "$TMP_PC" --just-inject="cat /pc/pack.zip!/small.txt"
assert_contains "$TETHER_STDOUT" "hello from a zip" "cat of a small zip entry"

log_test "stored 200 KB entry (larger than free heap) is read intact"
run_tether -pc "$TMP_PC" --just-inject="md5 /pc/pack.zip!/big.bin"
assert_exit_code 0 "$TETHER_EXIT" "md5 of the stored entry succeeded"
assert_equal "$(md5_of "$TMP_PC/src/big.bin")" "$(mdnum "$TETHER_STDOUT")" "md5 of the stored entry matches the PC"

log_test "deflated 100 KB entry is inflated as a stream"
run_tether -pc "$TMP_PC" --just-inject="md5 /pc/pack.zip!/text.txt"
assert_exit_code 0 "$TETHER_EXIT" "md5 of the deflated entry succeeded"
assert_equal "$(md5_of "$TMP_PC/src/text.txt")" "$(mdnum "$TETHER_STDOUT")" "md5 of the deflated entry matches the PC"

log_test "zip listing and sourcing a script from inside a zip"
printf 'set fromzip 99\n' > "$TMP_PC/src/s.tcl"
( cd "$TMP_PC/src" && zip -q -0 ../pack.zip s.tcl )
run_tether -pc "$TMP_PC" --just-inject="source /pc/pack.zip!/s.tcl; set fromzip"
assert_contains "$TETHER_STDOUT" "99" "source of a stored zip entry"

log_test "the Tcl zip command: small member works, big member is a polite error"
run_tether -pc "$TMP_PC" --just-inject="zip get /pc/pack.zip small.txt"
assert_contains "$TETHER_STDOUT" "hello from a zip" "zip get of a small member"
run_tether -pc "$TMP_PC" --just-inject="zip names /pc/pack.zip"
assert_contains "$TETHER_STDOUT" "big.bin" "zip names lists the members"
run_tether -pc "$TMP_PC" --just-inject="zip get /pc/pack.zip big.bin"
assert_exit_code 1 "$TETHER_EXIT" "zip get of a 200 KB member reports an error (no reboot)"
assert_contains "$TETHER_STDOUT$TETHER_STDERR" "too big" "error says the member is too big"
run_tether --just-ping=5
assert_contains "$TETHER_STDOUT" "just-ping: OK (value=5)" "board still answers"

log_test "multifile on a file too big for memory is refused"
run_tether -pc "$TMP_PC" --just-inject="multifile names {[} {]} /pc/src/big.bin"
assert_exit_code 1 "$TETHER_EXIT" "multifile refuses a 200 KB file"
assert_contains "$TETHER_STDOUT$TETHER_STDERR" "too big" "error says too big"

log_test "no memory is left allocated afterwards"
run_tether -pc "$TMP_PC" --just-inject="centipede heap-free"
FREE1=$(heapnum "$TETHER_STDOUT")
if [ -n "$FREE0" ] && [ -n "$FREE1" ] && [ "$FREE1" -gt $((FREE0 - 2048)) ]; then pass "free heap $FREE0 -> $FREE1"; else fail "heap leaked: $FREE0 -> $FREE1"; fi

log_test "after the heap is fragmented, a deflated entry fails CLEANLY (S51), the board survives"
# v4/early v5: malloc failure called panic() ("Out of memory") and the board hung until reset.  Now malloc
# returns NULL and the open simply fails.  Either outcome (works / clean error) is fine; a hang is not.
run_tether -pc "$TMP_PC" --just-inject="proc eat {} { for {set i 0} {1} {incr i} { set v\$i \$i } }; catch {eat} msg; set msg"
assert_contains "$TETHER_STDOUT" "out of memory" "heap exhausted on purpose"
run_tether -pc "$TMP_PC" --just-inject="md5 /pc/pack.zip!/text.txt"
case "$TETHER_EXIT" in 0|1) pass "deflated read after exhaustion returned (exit $TETHER_EXIT), no hang" ;; *) fail "deflated read after exhaustion: exit $TETHER_EXIT" ;; esac
run_tether --just-ping=9
assert_contains "$TETHER_STDOUT" "just-ping: OK (value=9)" "board still answers after a failed deflated open"

print_summary "Suite 5 (zip streaming)"
