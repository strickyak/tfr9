#!/bin/sh
# 09_centipede_coco2_test.sh -- Live hardware system test for Centipede 32z + CoCo 2 (POSIX sh).
# Selects `b=centipede v=32z` by flash label; skips cleanly if no Centipede 32z is attached.

BOARD_MATCH=${BOARD_MATCH:-"b=centipede v=32z"}
. "$(dirname "$0")/common.sh"

echo "Running Suite 9: Centipede 32z + CoCo 2 hardware tests"
if ! board_present; then
  skip "whole suite (no Centipede 32z board attached)"
  print_summary "Suite 9 (centipede32z)"
  exit $?
fi

TMP_OUT=$(mktemp -d "${TMPDIR:-/tmp}/systest_cen.XXXXXX")
trap 'rm -rf "$TMP_OUT"' EXIT INT TERM

# 1. Label, Ping, and Mode 27 Tcl REPL
log_test "1. Flash label, ping, heap-info, and default ports on Centipede 32z"
run_tether --quick-get-label
assert_exit_code 0 "$TETHER_EXIT" "quick-get-label"
assert_contains "$TETHER_STDOUT" "b=centipede" "label names b=centipede"
assert_contains "$TETHER_STDOUT" "v=32z" "label names v=32z"

run_tether --quick-ping=77
assert_exit_code 0 "$TETHER_EXIT" "quick-ping=77"
assert_contains "$TETHER_STDOUT" "quick-ping: OK (value=77)" "ping echoed 77"

run_tether --quick-restart=27
assert_exit_code 0 "$TETHER_EXIT" "quick-restart=27"
sleep 2

run_tether --quick-inject="centipede bootmode"
assert_contains "$TETHER_STDOUT" "27" "bootmode is 27"

run_tether --quick-inject="centipede heap-free"
assert_exit_code 0 "$TETHER_EXIT" "centipede heap-free"
HEAP_FREE=$(printf '%s\n' "$TETHER_STDOUT" | grep -o '[0-9][0-9]*[[:space:]]*$' | tail -1 | tr -d '[:space:]')
if [ "${HEAP_FREE:-0}" -ge 32768 ] 2>/dev/null; then
  pass "heap-free >= 32 KB ($HEAP_FREE bytes)"
else
  fail "heap-free too low: '$HEAP_FREE'"
fi

run_tether --quick-inject="ports list"
assert_contains "$TETHER_STDOUT" "on coco2 run rsbasic for coco2 engine gerbil_sam64k" "default Centipede target is coco2/rsbasic/coco2"

run_tether --quick-inject="ports check"
assert_contains "$TETHER_STDOUT" "ok" "default ports check is ok"

# 2. Host & Motherboard Collision Checks (`on`/`run`/`ports`)
log_test "2. Centipede host validation & CoCo motherboard PIA/SAM/GIME collision guards"
run_tether --quick-inject="catch {on tfr911h run turbo9os; bye} e; set e"
assert_contains "$TETHER_STDOUT" "Centipede cartridge" "on tfr911h refused at bye on Centipede"

run_tether --quick-inject="catch {on coco2 run nitros9l2 for coco3 with becker at 0xff14; ports check} e; set e"
assert_contains "$TETHER_STDOUT" "coco_pia" "CoCo 2 PIA (\$FF00-\$FF3F) collision caught"

run_tether --quick-inject="catch {on coco2 run nitros9l2 for coco3 with rtc at 0xffc0; ports check} e; set e"
assert_contains "$TETHER_STDOUT" "coco_sam" "CoCo 2 SAM (\$FFC0-\$FFDF) collision caught"

run_tether --quick-inject="catch {on coco3 run nitros9l2 for coco3 with rtc at 0xff90; ports check} e; set e"
assert_contains "$TETHER_STDOUT" "coco3_gime" "CoCo 3 real GIME (\$FF90-\$FFBF) collision caught"

run_tether --quick-inject="on coco2 run rsbasic for coco2 with e1 with becker with audio with rtc with orchestra90 with info; ports check"
assert_contains "$TETHER_STDOUT" "ok" "all 5 e1 enhancements validate cleanly on coco2/rsbasic"

# 3. `gerbil_sam64k`: CoCo 2 64 KB RAM + Disk Extended Color BASIC 1.1 + InfoPort + RtcPort
log_test "3. gerbil_sam64k: CoCo 2 Disk Extended Color BASIC 1.1 + 64K RAM + e1 InfoPort"
run_tether --quick-inject="on coco2 run rsbasic for coco2 with e1 with info with rtc; bye"
assert_exit_code 0 "$TETHER_EXIT" "launch coco2/rsbasic/coco2 with e1 with info with rtc"
sleep 2

run_tether --quick-get-text=0x0400,32,16
assert_contains "$TETHER_STDOUT" "DISK EXTENDED COLOR BASIC 1.1" "Disk Extended Color BASIC 1.1 banner on CoCo 2 VDG"

run_tether --quick-type="$(printf 'POKE &HFF7C,&HA5:?MEM;PEEK(&HFF7C);6*7\r')"
assert_exit_code 0 "$TETHER_EXIT" "quick-type MEM + InfoPort + 6*7"
sleep 9

run_tether --quick-get-text=0x0400,32,16
assert_contains "$TETHER_STDOUT" "22823" "64 KB Disk BASIC free memory (22823)"
assert_contains "$TETHER_STDOUT" " 1 " "InfoPort \$FF7C ready status = 1"
assert_contains "$TETHER_STDOUT" " 42" "6809E evaluated 6*7 = 42"

# 4. `gerbil_gime`: "Become CoCo 3" (`on coco2 run rsbasic for coco3`) + HSCREEN 2 capture
log_test "4. gerbil_gime: Become CoCo 3 (Super Extended BASIC 2.1 + HSCREEN 2)"
run_tether --quick-restart=27
assert_exit_code 0 "$TETHER_EXIT" "quick-restart=27 before coco3 mode"
sleep 2

run_tether --quick-inject="on coco2 run rsbasic for coco3; bye"
assert_exit_code 0 "$TETHER_EXIT" "launch coco2/rsbasic/coco3 (gerbil_gime)"
sleep 3

run_tether --quick-get-text=0x0400,32,16
assert_contains "$TETHER_STDOUT" "EXTENDED COLOR BASIC 2." "CoCo 3 Super Extended Color BASIC banner"

run_tether --quick-type="$(printf '?123*456\r')"
sleep 3
run_tether --quick-get-text=0x0400,32,16
assert_contains "$TETHER_STDOUT" "56088" "CoCo 3 BASIC evaluated 123*456 = 56088"

run_tether --quick-type="$(printf 'HSCREEN 2:HCLS 1:HCOLOR 4:HLINE(0,0)-(319,191),PSET\r')"
sleep 12
run_tether --quick-get-hscreen="2,$TMP_OUT/hscreen2.png"
assert_exit_code 0 "$TETHER_EXIT" "quick-get-hscreen=2"
if [ -s "$TMP_OUT/hscreen2.png" ]; then
  pass "captured non-empty HSCREEN 2 PNG ($(size_of "$TMP_OUT/hscreen2.png") bytes)"
else
  fail "HSCREEN 2 PNG missing or empty"
fi

# 5. DECB Preload (`centipede run-decb`) on real 6809E (`gerbil_sam64k` all-RAM mode)
SHELF=${SHELF:-$(cd "$V5/../.." && pwd)}
LWASM=$SHELF/bin/lwasm
log_test "5. DECB preload execution on real 6809E (centipede run-decb)"
if [ ! -x "$LWASM" ]; then
  skip "DECB preload test ($LWASM not found)"
else
  cat > "$TMP_OUT/hello_coco2.asm" << 'EOF'
        ORG     $2400
START   LDX     #MSG
        LDU     #$0400
LOOP    LDA     ,X+
        BEQ     DONE
        CMPA    #$40
        BLO     STORE
        SUBA    #$40
STORE   STA     ,U+
        BRA     LOOP
DONE    BRA     DONE
MSG     FCC     /V5 DECB PRELOAD OK ON 6809E!/
        FCB     0
        END     START
EOF
  (cd "$TMP_OUT" && "$LWASM" --6809 --format=decb -o hello_coco2.bin hello_coco2.asm && rm -f ./*.map ./*.list)
  run_tether --quick-restart=27
  sleep 2
  run_tether -pc="$TMP_OUT" --quick-inject="centipede run-decb /pc/hello_coco2.bin"
  assert_exit_code 0 "$TETHER_EXIT" "centipede run-decb /pc/hello_coco2.bin"
  sleep 1
  run_tether --quick-get-text=0x0400,32,16
  assert_contains "$TETHER_STDOUT" "V5 DECB PRELOAD OK ON 6809E!" "6809E executed DECB preload and wrote VDG screen"
fi

# Leave Centipede cleanly in Mode 27 REPL
run_tether --quick-restart=27
sleep 1

print_summary "Suite 9 (centipede32z)"
