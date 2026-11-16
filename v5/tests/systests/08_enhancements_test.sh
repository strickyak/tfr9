#!/bin/sh
# 08_enhancements_test.sh -- Phase 5 hardware verification on TFR/911H:
#   1. Tcl commands (`beep`, `click`, `fujinet stats`)
#   2. 6309 on `hamster_flat` (`nitros9l1 for flat65280 with e1, becker, audio, rtc, orchestra90, info`):
#      SN76489 FIFO + control, Orchestra-90 stereo DAC, Becker status/data, live T_WALLTIME RTC ($FF72..$FF79),
#      and core-0 InfoPort report stream ($FF7C/$FF7D), exiting cleanly via ClockStop ($FF87).
#   3. 6309 on `hamster_gime` (`nitros9l2 for coco3 with e1, info`):
#      Track-35 ('OS') $2500->$2602 launcher, Coco3Palette ($FFB0..$FFBF), GIME shadow regs ($FF94..$FF9F),
#      and real 60 Hz GIME VBORD hardware interrupt ($FF90/$FF92 + CWAI + $FFF8->$FEF7).
#   4. 6309 on `hamster_gime` (`nitros9l2 for jamesbrown`):
#      Direct boot from drive 0 Track 34 (LSN 612) with no Config(flat_img), talking over Diller's I/O map
#      (ACIA $FF14/$FF15, Audio $FF41/$FF42, Info $FF43/$FF44, Becker $FF45/$FF46, GIME $FF90..$FF93, RTC $FFC0..$FFC7).
. "$(dirname "$0")/common.sh"
echo "Running Suite 8: Phase 5 enhancement devices & nitros9l2 (coco3 / jamesbrown) targets"
if ! board_present; then skip "whole suite (no board attached)"; print_summary "Suite 8 (enhancements)"; exit $?; fi
if ! command -v expect > /dev/null 2>&1; then skip "whole suite (no expect command)"; print_summary "Suite 8 (enhancements)"; exit $?; fi

# Put $SHELF/bin on PATH so lwasm is found on shelf setups.
V5="$V5" . "$V5/scripts/env.sh"
if ! command -v lwasm > /dev/null 2>&1; then
  skip "whole suite (lwasm not found)"
  print_summary "Suite 8 (enhancements)"
  exit $?
fi

PC_DIR=$(mktemp -d "${TMPDIR:-/tmp}/systest_enh.XXXXXX")
trap 'rm -rf "$PC_DIR"' EXIT INT TERM

wait_board_ready() {
  _ok=0; _try=0; WIRE=
  while [ -z "$WIRE" ] && [ "$_try" -lt 20 ]; do find_wire; [ -n "$WIRE" ] || sleep 1; _try=$((_try + 1)); done
  [ -n "$WIRE" ] || return 1
  _try=0
  while [ "$_try" -lt 8 ]; do
    run_tether --quick-ping=55
    case "$TETHER_STDOUT" in *"quick-ping: OK"*) _ok=1; break ;; esac
    sleep 1; _try=$((_try + 1))
  done
  [ "$_ok" -eq 1 ]
}

# ── Test 1: Tcl commands (`click`, `beep`, `fujinet stats`) ─────────────────
log_test "Tcl enhancement commands (click, beep, fujinet stats)"
run_tether --quick-restart=0 > /dev/null
run_tether --quick-inject="click"
assert_contains "$TETHER_STDOUT" "on" "click defaults to on"
run_tether --quick-inject="click off"
assert_contains "$TETHER_STDOUT" "off" "click off disables keyclick"
run_tether --quick-inject="click on"
assert_contains "$TETHER_STDOUT" "on" "click on re-enables keyclick"
run_tether --quick-inject="beep 880 5; expr 3+4"
assert_contains "$TETHER_STDOUT" "7" "beep initializes PWM/alarm2 cleanly"
run_tether --quick-inject="fujinet stats"
assert_contains "$TETHER_STDOUT" "rx 0 tx 0" "fujinet stats reports counters"

# ── Test 2: 6309 on flat65280 + full `e1` enhancement map ──────────────────
log_test "6309 hardware test: flat65280 with e1 (becker, audio, rtc, orchestra90, info)"
cat > "$PC_DIR/e1_flat.asm" << 'EOF'
        ORG     $0000
        FILL    0,$1000-*
Start   ORCC    #$50
        LDS     #$7F00

        * 1. Audio SN76489 ($FF70 data, $FF71 ctrl)
        LDA     #$9F            * ch0 off
        STA     $FF70
        LDA     #$BF            * ch1 off
        STA     $FF70
        LDA     #$DF            * ch2 off
        STA     $FF70
        LDA     #$FF            * noise off
        STA     $FF70
        * Delay briefly so 31.25 kHz ISR drains all 4 FIFO writes
        LDX     #$2000
AudWait LEAX    -1,X
        BNE     AudWait

        LDA     #$82
        STA     $FF71
        LDB     $FF71           * expect $82
        PSHS    B
        LDA     #$03
        STA     $FF71
        LDB     $FF71           * expect $03
        PSHS    B
        LEAX    MsgAud,PCR
        LBSR    Puts
        PULS    A               * $03
        PULS    B               * $82
        EXG     A,B
        LBSR    PutHex          * print 82
        LDA     #',
        LBSR    Putc
        TFR     B,A
        LBSR    PutHex          * print 03
        LBSR    PutNL

        * 2. Orchestra-90 stereo DAC ($FF7A left, $FF7B right)
        LDA     #$5A
        STA     $FF7A
        LDA     #$A5
        STA     $FF7B
        LEAX    MsgOrch,PCR
        LBSR    Puts
        LDA     $FF7A
        LBSR    PutHex
        LDA     #',
        LBSR    Putc
        LDA     $FF7B
        LBSR    PutHex
        LBSR    PutNL

        * 3. Becker port ($FF41 status, $FF42 data)
        LEAX    MsgBeck,PCR
        LBSR    Puts
        LDA     $FF41           * expect $00
        LBSR    PutHex
        LDA     #',
        LBSR    Putc
        LDA     $FF42           * expect $FF (empty read)
        LBSR    PutHex
        LBSR    PutNL

        * 4. RTC port ($FF72..$FF79: century, year, month, day, ...)
        LEAX    MsgRtc,PCR
        LBSR    Puts
        LDA     $FF72           * century (20 = $14)
        LBSR    PutHex
        LDA     #',
        LBSR    Putc
        LDA     $FF73           * year (26 = $1A)
        LBSR    PutHex
        LDA     #',
        LBSR    Putc
        LDA     $FF74           * month (1..12)
        LBSR    PutHex
        LBSR    PutNL

        * 5. Info port ($FF7C trigger/status, $FF7D stream)
        LDA     #$A5
        STA     $FF7C
InfoWt  LDA     $FF7C
        BEQ     InfoWt
InfoLp  LDA     $FF7D
        BEQ     InfoDn
        LBSR    Putc
        BRA     InfoLp
InfoDn  LEAX    MsgDone,PCR
        LBSR    Puts

        * Clean shutdown via ClockStop ($FF87 = $FC)
        LDA     #$FC
        STA     $FF87
Hang    BRA     Hang

Putc    STA     $FF89
        RTS

PutNL   LDA     #13
        BSR     Putc
        LDA     #10
        BRA     Putc

Puts    LDA     ,X+
        BEQ     PutsEnd
        BSR     Putc
        BRA     Puts
PutsEnd RTS

PutHex  PSHS    A
        LSRA
        LSRA
        LSRA
        LSRA
        BSR     PutNyb
        PULS    A
        ANDA    #$0F
PutNyb  CMPA    #10
        BLO     PutDig
        ADDA    #'A-10
        BRA     Putc
PutDig  ADDA    #'0
        BRA     Putc

MsgAud  FCC     "AUD="
        FCB     0
MsgOrch FCC     "ORCH="
        FCB     0
MsgBeck FCC     "BECK="
        FCB     0
MsgRtc  FCC     "RTC="
        FCB     0
MsgDone FCC     "E1_FLAT_OK"
        FCB     13,10,0

        FILL    0,$FFF0-*
        FDB     Start,Start,Start,Start,Start,Start,Start,Start
EOF
(cd "$PC_DIR" && lwasm --format=raw -o e1_flat.img e1_flat.asm >/dev/null 2>&1) || { fail "lwasm failed on e1_flat.asm"; exit 1; }
cat > "$PC_DIR/mode91.tcl" << 'EOF'
set Config(flat_img) /pc/e1_flat.img
menu store Config
on tfr911h run nitros9l1 for flat65280 with e1 with becker with audio with rtc with orchestra90 with info
bye
EOF

out=$(TETHER="$TETHER" PC_DIR="$PC_DIR" WIRE="$WIRE" expect -c '
  set timeout 20
  spawn $env(TETHER) -n -bind= -wire=$env(WIRE) -pc=$env(PC_DIR) -bootmode=91
  expect {
    "target shutdown OK (0)" { exit 0 }
    timeout { puts "TIMEOUT waiting for E1_FLAT_OK"; exit 1 }
    eof { exit 1 }
  }
' < /dev/null 2>&1)
rc=$?
mkdir -p "$BUILD_DIR/systest-logs" && printf '%s\n' "$out" > "$BUILD_DIR/systest-logs/enh-e1-flat.log"
assert_exit_code 0 "$rc" "e1_flat 6309 run exited cleanly via ClockStop"
assert_contains "$out" "AUD=82,03" "AudioSn76489 control register R/W"
assert_contains "$out" "ORCH=5A,A5" "Orchestra90 stereo DAC R/W"
assert_contains "$out" "BECK=00,FF" "BeckerPort idle status and empty read"
assert_contains "$out" "RTC=14," "RtcPort century=20 (0x14) from T_WALLTIME"
assert_contains "$out" "Target	tfr911h/nitros9l1/flat65280 (engine hamster_flat)" "InfoPort reports target & engine"
assert_contains "$out" "Sound	SN76489 at 0xff70 (4 writes)" "InfoPort reports 4 SN76489 writes processed by ISR"
assert_contains "$out" "Clock	set from PC via tether" "InfoPort confirms T_WALLTIME clock sync"
assert_contains "$out" "E1_FLAT_OK" "e1_flat 6309 test reached completion"

wait_board_ready || { fail "board did not recover after e1_flat"; print_summary "Suite 8 (enhancements)"; exit 1; }

# ── Test 3: 6309 on `nitros9l2 for coco3` (.t35 Track-35 boot + GIME VBORD IRQ + Palette) ──
log_test "6309 hardware test: nitros9l2 for coco3 (.t35 launcher, GIME VBORD 60Hz IRQ, Coco3Palette)"
cat > "$PC_DIR/coco3_t35.asm" << 'EOF'
        ORG     $2600
        FCC     "OS"            * Track-35 signature; firmware DOS stub at $2500 jumps to $2602
Entry   ORCC    #$50
        LDS     #$1F00

        * Install JMP IrqSvc at CoCo 3 secondary IRQ vector ($FEF7: $7E hi lo)
        LDA     #$7E
        STA     $FEF7
        LEAX    IrqSvc,PCR
        STX     $FEF8

        * Test Coco3Palette ($FFB0..$FFBF, 6-bit mask) and GIME shadow ($FF98)
        LDA     #$35
        STA     $FFB0
        LDA     #$EA            * masked to $2A
        STA     $FFBF
        LDA     #$77
        STA     $FF98

        LEAX    MsgPal,PCR
        BSR     Puts
        LDA     $FFB0
        BSR     PutHex
        LDA     #',
        BSR     Putc
        LDA     $FFBF
        BSR     PutHex
        LDA     #',
        BSR     Putc
        LDA     $FF98
        BSR     PutHex
        BSR     PutNL

        * Enable GIME MMUEN ($40) + IEN ($20) at $FF90, and VBORD ($08) at $FF92
        CLR     Irqst1
        CLR     Irqst2
        LDA     #$60
        STA     $FF90
        LDA     #$08
        STA     $FF92

        * Wait for real 60 Hz GIME VBORD hardware IRQ on GPIO 22!
        CWAI    #$EF

        LEAX    MsgIrq,PCR
        BSR     Puts
        LDA     Irqst1          * expect $08 (pending VBORD bit)
        BSR     PutHex
        LDA     #',
        BSR     Putc
        LDA     Irqst2          * expect $00 (cleared on first read!)
        BSR     PutHex
        BSR     PutNL

        * Stream InfoPort ($FF7C/$FF7D from `with e1, info`)
        LDA     #$A5
        STA     $FF7C
InfWt   LDA     $FF7C
        BEQ     InfWt
InfLp   LDA     $FF7D
        BEQ     InfDn
        BSR     Putc
        BRA     InfLp
InfDn   LEAX    MsgDone,PCR
        BSR     Puts
Spin    BRA     Spin

IrqSvc  LDA     $FF92           * reads $08 and clears pending latch
        STA     Irqst1
        LDA     $FF92           * reads $00 now
        STA     Irqst2
        CLR     $FF92           * disable VBORD
        RTI

Irqst1  FCB     $FF
Irqst2  FCB     $FF

Putc    STA     $FF89
        RTS
PutNL   LDA     #13
        BSR     Putc
        LDA     #10
        BRA     Putc
Puts    LDA     ,X+
        BEQ     PutsEnd
        BSR     Putc
        BRA     Puts
PutsEnd RTS
PutHex  PSHS    A
        LSRA
        LSRA
        LSRA
        LSRA
        BSR     PutNyb
        PULS    A
        ANDA    #$0F
PutNyb  CMPA    #10
        BLO     PutDig
        ADDA    #'A-10
        BRA     Putc
PutDig  ADDA    #'0
        BRA     Putc

MsgPal  FCC     "PAL="
        FCB     0
MsgIrq  FCC     "GIME_IRQ="
        FCB     0
MsgDone FCC     "COCO3_T35_OK"
        FCB     13,10,0
EOF
(cd "$PC_DIR" && lwasm --format=raw -o coco3_test.t35 coco3_t35.asm >/dev/null 2>&1) || { fail "lwasm failed on coco3_t35.asm"; exit 1; }
cat > "$PC_DIR/mode92.tcl" << 'EOF'
set Config(flat_img) /pc/coco3_test.t35
menu store Config
on tfr911h run nitros9l2 for coco3 with e1 with info
bye
EOF

out=$(TETHER="$TETHER" PC_DIR="$PC_DIR" WIRE="$WIRE" expect -c '
  set timeout 20
  spawn $env(TETHER) -n -bind= -wire=$env(WIRE) -pc=$env(PC_DIR) -bootmode=92
  expect {
    "COCO3_T35_OK" { exit 0 }
    timeout { puts "TIMEOUT waiting for COCO3_T35_OK"; exit 1 }
    eof { exit 1 }
  }
' < /dev/null 2>&1)
rc=$?
mkdir -p "$BUILD_DIR/systest-logs" && printf '%s\n' "$out" > "$BUILD_DIR/systest-logs/enh-coco3.log"
assert_exit_code 0 "$rc" "nitros9l2 for coco3 (.t35) booted and completed"
assert_contains "$out" "PAL=35,2A,77" "Coco3Palette 6-bit mask and GIME shadow readback"
assert_contains "$out" "GIME_IRQ=08,00" "60 Hz GIME VBORD hardware IRQ fired and cleared on read"
assert_contains "$out" "Target	tfr911h/nitros9l2/coco3 (engine hamster_gime)" "InfoPort reports coco3 target"

run_tether --quick-restart=0 > /dev/null
wait_board_ready || { fail "board did not recover after coco3 test"; print_summary "Suite 8 (enhancements)"; exit 1; }

# ── Test 4: 6309 on `nitros9l2 for jamesbrown` (Track-34 disk boot + Diller I/O map) ──
log_test "6309 hardware test: nitros9l2 for jamesbrown (Track-34 disk boot + Diller I/O map)"
cat > "$PC_DIR/jb_t34.asm" << 'EOF'
        ORG     $2600
        FCC     "OS"            * Placed on disk Track 34 (LSN 612); no Config(flat_img)!
Entry   ORCC    #$50
        LDS     #$1F00

        * Install JMP IrqSvc at $FEF7
        LDA     #$7E
        STA     $FEF7
        LEAX    IrqSvc,PCR
        STX     $FEF8

        * Test Audio ($FF41/$FF42), Becker ($FF45/$FF46), and RTC ($FFC0..$FFC7)
        LDA     #$9F
        STA     $FF41
        LDA     #$83
        STA     $FF42

        LEAX    MsgJb,PCR
        BSR     Puts
        LDA     $FF42           * Audio ctrl ($83)
        BSR     PutHex
        LDA     #',
        BSR     Putc
        LDA     $FF45           * Becker status ($00)
        BSR     PutHex
        LDA     #',
        BSR     Putc
        LDA     $FFC0           * RTC century ($14 = 20)
        BSR     PutHex
        BSR     PutNL

        * Test 4-byte GIME VBORD IRQ ($FF90/$FF92)
        LDA     #$60
        STA     $FF90
        LDA     #$08
        STA     $FF92
        CWAI    #$EF

        * Stream InfoPort at Diller's $FF43/$FF44
        LDA     #$A5
        STA     $FF43
JbWt    LDA     $FF43
        BEQ     JbWt
JbLp    LDA     $FF44
        BEQ     JbDn
        BSR     Putc
        BRA     JbLp
JbDn    LEAX    MsgOk,PCR
        BSR     Puts
JbSpin  BRA     JbSpin

IrqSvc  LDA     $FF92
        CLR     $FF92
        RTI

* Console TX on JamesBrown is $FF15!
Putc    STA     $FF15
        RTS
PutNL   LDA     #13
        BSR     Putc
        LDA     #10
        BRA     Putc
Puts    LDA     ,X+
        BEQ     PutsEnd
        BSR     Putc
        BRA     Puts
PutsEnd RTS
PutHex  PSHS    A
        LSRA
        LSRA
        LSRA
        LSRA
        BSR     PutNyb
        PULS    A
        ANDA    #$0F
PutNyb  CMPA    #10
        BLO     PutDig
        ADDA    #'A-10
        BRA     Putc
PutDig  ADDA    #'0
        BRA     Putc

MsgJb   FCC     "JB="
        FCB     0
MsgOk   FCC     "JAMESBROWN_ALL_OK"
        FCB     13,10,0

        FILL    0,$3800-*       * Pad to 18 sectors (4608 bytes)
EOF
(cd "$PC_DIR" && lwasm --format=raw -o jb_t34.bin jb_t34.asm >/dev/null 2>&1) || { fail "lwasm failed on jb_t34.asm"; exit 1; }
# Build a minimal disk image with Track 34 (LSN 612..629) populated.
dd if=/dev/zero of="$PC_DIR/jb.dsk" bs=256 count=630 2>/dev/null
dd if="$PC_DIR/jb_t34.bin" of="$PC_DIR/jb.dsk" bs=256 seek=612 conv=notrunc 2>/dev/null

cat > "$PC_DIR/mode93.tcl" << 'EOF'
set Config(drive0) /pc/jb.dsk
on tfr911h run nitros9l2 for jamesbrown
bye
EOF

out=$(TETHER="$TETHER" PC_DIR="$PC_DIR" WIRE="$WIRE" expect -c '
  set timeout 20
  spawn $env(TETHER) -n -bind= -wire=$env(WIRE) -pc=$env(PC_DIR) -bootmode=93
  expect {
    "JAMESBROWN_ALL_OK" { exit 0 }
    timeout { puts "TIMEOUT waiting for JAMESBROWN_ALL_OK"; exit 1 }
    eof { exit 1 }
  }
' < /dev/null 2>&1)
rc=$?
mkdir -p "$BUILD_DIR/systest-logs" && printf '%s\n' "$out" > "$BUILD_DIR/systest-logs/enh-jamesbrown.log"
assert_exit_code 0 "$rc" "nitros9l2 for jamesbrown booted from Track 34 on drive 0"
assert_contains "$out" "JB=83,00,14" "JamesBrown Audio (\$FF42), Becker (\$FF45), and RTC (\$FFC0)"
assert_contains "$out" "Target	tfr911h/nitros9l2/jamesbrown (engine hamster_gime)" "JamesBrown InfoPort (\$FF43/\$FF44)"
assert_contains "$out" "FujiNet	becker at 0xff45" "JamesBrown Becker reported at 0xff45"
assert_contains "$out" "Sound	SN76489 at 0xff41" "JamesBrown SN76489 reported at 0xff41"

run_tether --quick-restart=0 > /dev/null
wait_board_ready || { fail "board did not recover after jamesbrown test"; print_summary "Suite 8 (enhancements)"; exit 1; }

print_summary "Suite 8 (enhancements)"
