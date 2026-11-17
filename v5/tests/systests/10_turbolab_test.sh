#!/bin/sh
# 10_turbolab_test.sh -- Live hardware system test for `ON tfr911h RUN turbolab WITH ...` (POSIX sh).
# Tests cycle-accurate tracing, `trigger`, `trace_count`, `stop`, `watch`, `.list` annotations,
# OS-9 `@module+offset` symbols & SWI2/RTI syscall decoding, Red-Page / BRA-self faults,
# Phase-4 SWI 6809 & 6309 native register capture, and 64 KB `/tmp/fault.img` core dumps!

. "$(dirname "$0")/common.sh"

echo "Running Suite 10: TurboLab on TFR/911H (ON tfr911h RUN turbolab WITH ...)"
if ! board_present; then
  skip "whole suite (no TFR/911H board attached)"
  print_summary "Suite 10 (turbolab)"
  exit $?
fi

V5="$V5" . "$V5/scripts/env.sh"
if ! command -v lwasm >/dev/null 2>&1; then
  skip "whole suite (lwasm not found)"
  print_summary "Suite 10 (turbolab)"
  exit $?
fi

PC_DIR=$(mktemp -d "${TMPDIR:-/tmp}/systest_turbolab.XXXXXX")
trap 'rm -rf "$PC_DIR"' EXIT INT TERM

wait_board_ready() {
  sleep 2
  _ok=0; _try=0; WIRE=
  while [ -z "$WIRE" ] && [ "$_try" -lt 20 ]; do find_wire; [ -n "$WIRE" ] || sleep 1; _try=$((_try + 1)); done
  [ -n "$WIRE" ] || return 1
  _try=0
  while [ "$_try" -lt 10 ]; do
    run_tether --just-ping=88
    case "$TETHER_STDOUT" in *"just-ping: OK"*) _ok=1; break ;; esac
    sleep 1; find_wire; _try=$((_try + 1))
  done
  [ "$_ok" -eq 1 ]
}

# Helper: stage mode88.tcl and run `tether -n -pc=$PC_DIR -bootmode=88 [extra tether flags...]`
run_tl() {
  _tcl=$1
  shift
  printf '%s\nbye\n' "$_tcl" > "$PC_DIR/mode88.tcl"
  rm -f /tmp/fault.img
  run_tether -pc="$PC_DIR" -bootmode=88 "$@"
  _tl_out=$TETHER_STDOUT
  _tl_err=$TETHER_STDERR
  _tl_rc=$TETHER_EXIT
  wait_board_ready || fail "board did not recover after '$_tcl'"
  TETHER_STDOUT=$_tl_out
  TETHER_STDERR=$_tl_err
  TETHER_EXIT=$_tl_rc
}

# Assemble `pi.decb` and `pi.decb.list` from imported `tests/systests/testdata/pi.asm`
(cd "$PC_DIR" && lwasm --6809 --format=decb -o pi.decb --list=pi.decb.list "$V5/tests/systests/testdata/pi.asm") || {
  fail "lwasm failed on pi.asm"
  print_summary "Suite 10 (turbolab)"
  exit 1
}

# 1. Strick's exact syntax: `ON tfr911h RUN turbolab WITH trigger = r/0xFFFE WITH trace_count = 15`
log_test "1. Trigger on reset vector (r/0xFFFE) + trace_count = 15 + 64KB core dump"
run_tl 'ON tfr911h RUN turbolab WITH trigger = r/0xFFFE WITH trace_count = 15'
assert_exit_code 0 "$TETHER_EXIT" "clean exit 0 on trace_count limit"
assert_contains "$TETHER_STDERR" "r FFFE " "Cycle #1 reset vector high byte traced"
assert_contains "$TETHER_STDERR" "reset vector (high)" "reset vector high annotated"
assert_contains "$TETHER_STDERR" "reset vector (low)" "reset vector low annotated"
assert_contains "$TETHER_STDERR" "Trace Count Limit Reached" "stopped on Trace Count Limit Reached"
assert_contains "$TETHER_STDERR" "CPU Registers (SWI Capture, 6809 mode)" "Phase-4 SWI 6809 register capture"
if [ -f /tmp/fault.img ] && [ "$(size_of /tmp/fault.img)" -eq 65536 ]; then
  pass "/tmp/fault.img written and is 65536 bytes"
else
  fail "/tmp/fault.img missing or wrong size"
fi

# 2. Strick's Red-Page address `r/0xFFE8`: trigger on `r/0xFFE8` + Red-Page fault + SWI register capture!
log_test "2. Strick's exact command: ON tfr911h RUN turbolab WITH trigger = r/0xFFE8 WITH trace_count = 10000"
cat > "$PC_DIR/redpage.asm" << 'EOF'
        ORG     $1000
START   LDS     #$0800
        LDX     #$CAFE
        LDY     #$BEEF
        LDA     $FFE8           * Reads Red Page ($FF04..$FFEF)!
        NOP
        END     START
EOF
(cd "$PC_DIR" && lwasm --6809 --format=decb -o redpage.decb redpage.asm) || fail "lwasm redpage.asm"
run_tl 'ON tfr911h RUN turbolab WITH image = /pc/redpage.decb WITH trigger = r/0xFFE8 WITH trace_count = 10000'
assert_exit_code 1 "$TETHER_EXIT" "Red-Page fault exits with code 1"
assert_contains "$TETHER_STDERR" "r FFE8 " "trigger = r/0xFFE8 captured the offending Red-Page read cycle"
assert_contains "$TETHER_STDERR" "Red Page Access" "reported Red Page Access fault"
assert_contains "$TETHER_STDERR" "Addr=\$FFE8" "fault address is \$FFE8"
assert_contains "$TETHER_STDERR" "X: \$CAFE   Y: \$BEEF" "SWI captured exact X=\$CAFE and Y=\$BEEF registers"

# 3. `pi.decb` + `.list` source annotation, cycle trigger (`c/60`), and Nth exec watchpoint (`x/0x1019/3`)
log_test "3. pi.decb cycle trigger, Nth execution watchpoint, and .list disassembly annotation"
run_tl 'ON tfr911h RUN turbolab WITH image = /pc/pi.decb WITH trace = x WITH stop = c/50' -listings="$PC_DIR/pi.decb.list"
assert_exit_code 0 "$TETHER_EXIT" "pi.decb trace=x stop=c/50 exited 0"
assert_contains "$TETHER_STDERR" "START: ldb #N" "annotated source line from pi.decb.list"
assert_not_contains "$TETHER_STDERR" "+ " "no '+' records when trace = x"
assert_not_contains "$TETHER_STDERR" "w " "no 'w' records when trace = x"

run_tl 'ON tfr911h RUN turbolab WITH image = /pc/pi.decb WITH trigger = x/0x1019/3 WITH trace = x WITH stop = c/120'
assert_exit_code 0 "$TETHER_EXIT" "Nth watchpoint trigger exited 0"
assert_not_contains "$TETHER_STDERR" "x 1019 ED #37;" "1st hit (#37) suppressed before 3rd-hit trigger"
assert_not_contains "$TETHER_STDERR" "x 1019 ED #53;" "2nd hit (#53) suppressed before 3rd-hit trigger"
assert_contains "$TETHER_STDERR" "x 1019 ED #69;" "3rd hit (#69) triggered trace output"

# 4. Silent logging watchpoints (`WITH watch = ... WITH watch = ...` without `trace`)
log_test "4. Repeated WITH watch = ... silent watchpoint logging"
run_tl 'ON tfr911h RUN turbolab WITH image = /pc/pi.decb WITH watch = 0x1000 WITH watch = 0x1002 WITH stop = c/200'
assert_exit_code 0 "$TETHER_EXIT" "silent watchpoints exited 0"
assert_contains "$TETHER_STDERR" "x 1000 C6 #4;" "watched \$1000 emitted"
assert_contains "$TETHER_STDERR" "x 1002 34 #6;" "watched \$1002 emitted"
wcount=$(printf '%s\n' "$TETHER_STDERR" | grep -c -E '^[x+rw-] [0-9A-Fa-f]{4} ' || true)
if [ "$wcount" -eq 2 ]; then
  pass "only the 2 watched cycles were emitted across 200 cycles"
else
  fail "expected 2 watched cycles, got $wcount"
fi

# 5. Built-in Turbo9OS: symbolic `@kernel+0x1B` stop AND `SWI2` / `RTI` OS-9 syscall decoding (`WITH trace = i`)
log_test "5. Built-in Turbo9OS: @kernel+0x1B symbol resolution and SWI2/RTI OS-9 API decoding"
run_tl 'ON tfr911h RUN turbolab WITH stop = @kernel+0x1B'
assert_exit_code 0 "$TETHER_EXIT" "stop = @kernel+0x1B exited 0"
assert_contains "$TETHER_STDERR" "Watchpoint Limit Reached" "stopped at @kernel+0x1B watchpoint"

# Built-in Turbo9OS includes Basic09 (~34 KB of primordial modules; CRC-24 boot scan runs before first SWI2)
run_tl 'ON tfr911h RUN turbolab WITH trace = i WITH stop = c/1500k'
assert_exit_code 0 "$TETHER_EXIT" "Turbo9OS trace=i stop=c/1500k exited 0"
assert_contains "$TETHER_STDERR" "Found primordial module" "tether scanned primordial OS-9 modules in initial RAM"
assert_contains "$TETHER_STDERR" "i SWI2 _1_" "SWI2 OS-9 syscall traced with label _1_"
assert_contains "$TETHER_STDERR" "F\$Link" "decoded F\$Link OS-9 system call"
assert_contains "$TETHER_STDERR" '"Init"' "decoded OS-9 string argument Init from shadow RAM"
assert_contains "$TETHER_STDERR" "i RTI  _1_" "matching RTI traced with label _1_"

# 6. Hitachi 6309 native-mode (`LDMD #1`) 14-byte SWI register capture + `BRA *` panic trap!
log_test "6. Hitachi 6309 native mode (LDMD #1) 14-byte W/E/F SWI register capture & BRA * fault"
cat > "$PC_DIR/native6309.asm" << 'EOF'
        ORG     $1000
START   LDS     #$0800
        FCB     $11,$3D,$01     * LDMD #$01 -> enter 6309 native mode!
        LDD     #$1234
        FCB     $10,$86,$56,$78 * LDW #$5678 (E=$56, F=$78)
        LDX     #$9ABC
SPIN    BRA     SPIN            * $20 $FE -> triggers FAULT_BRA_SELF!
        END     START
EOF
(cd "$PC_DIR" && lwasm --6309 --format=decb -o native6309.decb native6309.asm) || fail "lwasm native6309.asm"
run_tl 'ON tfr911h RUN turbolab WITH image = /pc/native6309.decb WITH stop = c/500'
assert_exit_code 1 "$TETHER_EXIT" "BRA * panic exits with code 1"
assert_contains "$TETHER_STDERR" "Infinite Loop (BRA \$FE)" "detected BRA \$FE infinite self-branch"
assert_contains "$TETHER_STDERR" "CPU Registers (SWI Capture, 6309 native mode)" "detected 6309 native 14-byte frame"
assert_contains "$TETHER_STDERR" "D: \$1234 (A=\$12 B=\$34)   W: \$5678 (E=\$56 F=\$78)" "captured 6309 D=\$1234 and W=\$5678 (E/F)"

print_summary "Suite 10 (turbolab)"
