#!/bin/sh
# make-jamesbrown-32z-dsk.sh -- build CoCo-2 / CoCo-3 I/O-safe Motorola 6809E
# build/centipede32z-jamesbrown.dsk for run-centipede32z-jamesbrown.sh.
#
# Stages a clean build workspace in build/jamesbrown-32z-work/ from Andrew Diller's
# nitros9-l2 sources + $SHELF/nitros9 + v5's CoCo-safe overrides in os/jamesbrown-32z/:
#   - Console ACIA (`sc6850` /Term):       0xFF88-0xFF89 (moved from 0xFF14 PIA0)
#   - FujiNet Becker (`dwio`, `boot_dw`):  0xFF41-0xFF42 (moved from 0xFF45)
#   - SN76489 Audio + Ctrl (`click`, etc): 0xFF70-0xFF71 (moved from 0xFF41-0xFF42)
#   - Wall-Clock RtcPort (`clock2_e1`):    0xFF72-0xFF79 (moved from 0xFFC0 SAM)
#   - Firmware InfoPort (`info`):          0xFF7C-0xFF7D (moved from 0xFF43-0xFF44)
#   - EmuDsk (`boot_emu`, `emudsk`):       0xFF80-0xFF86 (non-blocking byte FIFO $10/$11/$12)
#   - Clock (`clock.asm`):                 PIA0 60Hz VSYNC _IRQ + DoPoll every tick
#   - REL (`rel.asm`):                     SAM $FFD8 (0.895 MHz, safe for 16KB CoCo 2)
#   - CPU target:                          strict Motorola 6809 (`--6809 -DH6309=0`)
#   - Adds os/nitros9/cmds/basic09 to CMDS/basic09 with attributes r e pr pe.
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"

L2_SRC=${DILLERA_L2_DIR:-$HOME/github.com/dillera/tfr9/v4/nitros9-l2}
BASIC09_MOD="$V5/os/nitros9/cmds/basic09"
WORK="$V5/build/jamesbrown-32z-work"
OUT=${1:-$V5/build/centipede32z-jamesbrown.dsk}

if [ ! -d "$L2_SRC" ]; then
  echo "error: Diller nitros9-l2 source tree not found at $L2_SRC (set DILLERA_L2_DIR)" >&2
  exit 1
fi
if [ ! -f "$BASIC09_MOD" ]; then
  echo "error: missing $BASIC09_MOD" >&2
  exit 1
fi

rm -rf "$WORK"
mkdir -p "$WORK"

# Copy source directories/files only (never Diller's build/ or doc/).
for item in Makefile rmake-l2.tcl unix.tcl startup helpmsg.unix launcher.asm gen-rom-header.py modules games fujinet; do
  if [ -e "$L2_SRC/$item" ]; then
    cp -R "$L2_SRC/$item" "$WORK/$item"
  fi
done

# Overlay v5 CoCo-safe 6809 modules (rel, boot_emu, emudsk, clock, clock2_e1, click, dwread_becker_to).
cp "$V5"/os/jamesbrown-32z/modules/*.asm "$WORK/modules/"
cp "$V5/os/jamesbrown-32z/modules/dwread_becker_to.asm" "$WORK/fujinet/defs/"
rm -f "$WORK/fujinet/fujinet-lib/bus/os9/fuji_bus_call.s"

# Relocate all conflicting $FFxx addresses and switch assembler flags to strict --6809.
python3 - "$WORK" << 'PY'
import pathlib, sys

work = pathlib.Path(sys.argv[1])

def patch(rel, subs):
    p = work / rel
    txt = p.read_text()
    for old, new in subs:
        if old not in txt:
            raise SystemExit(f"make-jamesbrown-32z-dsk: expected {old!r} in {rel}")
        txt = txt.replace(old, new)
    p.write_text(txt)

patch("rmake-l2.tcl", [
    ("--6309", "--6809 -DH6309=0"),
    ("BECKBASE=0xFF45", "BECKBASE=0xFF41"),
    ("HwBASE=0xFF14", "HwBASE=0xFF88"),
    ("clock2_tfr911", "clock2_e1"),
    ("-sectors 99999", "-sectors 16384"),
])

patch("Makefile", [
    ("--6309", "--6809 -DH6309=0"),
])

patch("unix.tcl", [
    ("NitrOS-9 Level 2 on the TFR/911H", "NitrOS-9 Level 2 (jamesbrown-32z) on CoCo / Centipede 32z"),
])

(work / "startup").write_text(
    "echo NitrOS-9 Level 2 (jamesbrown-32z) on CoCo / Centipede 32z.  Unix-style commands: man unix\n"
)

patch("games/info.c", [
    ("0xFF43", "0xFF7C"),
    ("0xFF44", "0xFF7D"),
    ("$FF43", "$FF7C"),
    ("$FF44", "$FF7D"),
    ("Hardware (from the TFR/911H firmware)", "Hardware (from the Centipede 32z firmware)"),
    (" on the TFR/911H", " on CoCo / Centipede 32z"),
    (", as the TFR/911H pretends to be", " (CoCo / Centipede 32z)"),
    ("the clock runs from the GIME's VBORD IRQ", "the clock runs from PIA0 60Hz VSYNC IRQ"),
    (
        "        clusters = total / spc;\n",
        "        if (!total || !mapbytes || mapbytes > 8192 || (spc & (spc - 1)) ||\n"
        "            lsn0[0x1B] < 1 || lsn0[0x1B] > 12 || lsn0[0x1C] < 1 || lsn0[0x1C] > 31) {\n"
        "            put(\"not an OS-9 disk\");\n"
        "            endline();\n"
        "            os9_close(path);\n"
        "            continue;\n"
        "        }\n"
        "        clusters = total / spc;\n",
    ),
])

patch("fujinet/modules/dwio.asm", [
    ("                    use       dwread.asm", "                    use       dwread_becker_to.asm"),
])

# Drain any stale late RX bytes in Becker ($FF41/$FF42) before starting a new SS.Fuji
# transaction, and use a fast (~80ms) probe timeout for FUJI$Ready ($E2 $00) when no
# hat is attached, vs a ~31s (0.895 MHz) / ~16s (1.79 MHz) initial-byte timeout for
# active FujiNet commands so WiFi scans (~4-5s) and WiFi association/DHCP (~5-15s)
# never time out mid-command.
patch("fujinet/modules/scdwv.asm", [
    (
        "                    orcc      #IntMasks           freeze the wire for the whole exchange\n"
        "                    ldx       3,s                 system buffer\n"
        "                    leax      2,x                 request bytes follow the prefix\n"
        "                    ldy       9,s                 request length\n"
        "                    ldu       <D.DWSubAddr\n"
        "                    jsr       DW$Write,u          send request\n"
        "                    ldy       1,s                 response length\n"
        "                    beq       FujiOut2            command wants no reply\n"
        "                    ldx       3,s                 reuse the system buffer for the response\n"
        "                    jsr       DW$Read,u           read raw response\n"
        "                    bcs       FujiIOE2\n"
        "                    bne       FujiIOE2\n",
        "                    orcc      #IntMasks           freeze the wire for the whole exchange\n"
        "FujiDrn@            ldb       >$FF41\n"
        "                    bitb      #$02\n"
        "                    beq       FujiDrnOk@\n"
        "                    ldb       >$FF42\n"
        "                    bra       FujiDrn@\n"
        "FujiDrnOk@          ldx       3,s                 system buffer\n"
        "                    leax      2,x                 request bytes follow the prefix\n"
        "                    ldy       9,s                 request length\n"
        "                    ldu       <D.DWSubAddr\n"
        "                    jsr       DW$Write,u          send request\n"
        "                    ldy       1,s                 response length\n"
        "                    beq       FujiOut2            command wants no reply\n"
        "                    ldu       3,s                 system buffer ([resplen:2][req0][req1]...)\n"
        "                    ldd       2,u                 peek first 2 request bytes before overwriting\n"
        "                    cmpd      #$E200              OP_FUJI + FUJI$Ready?\n"
        "                    bne       FujiWaitLong@\n"
        "                    lda       #1                  fast ~80ms probe when checking if hat exists\n"
        "                    ldx       #$1000\n"
        "                    bra       FujiPoll@\n"
        "FujiWaitLong@       lda       #24                 ~31s (0.895 MHz) / ~16s (1.79 MHz) for scan/connect\n"
        "FujiNextByte@       ldx       #$0000              65536 inner polls per outer tick\n"
        "FujiPoll@           ldb       >$FF41\n"
        "                    bitb      #$02\n"
        "                    bne       FujiGotByte@\n"
        "                    leax      -1,x\n"
        "                    bne       FujiPoll@\n"
        "                    deca\n"
        "                    bne       FujiNextByte@\n"
        "                    bra       FujiIOE2\n"
        "FujiGotByte@        ldb       >$FF42\n"
        "                    stb       ,u+\n"
        "                    lda       #2                  inter-byte safety timeout once streaming\n"
        "                    leay      -1,y\n"
        "                    bne       FujiNextByte@\n",
    ),
])

# Upstream FujiNet firmware returns 0 scan results (esp_wifi_scan_start error 12294 =
# ESP_ERR_WIFI_STATE) if a scan starts while WiFiManager is mid-reconnect to an
# unreachable saved SSID (~8s burst followed by 10s pause). Retry empty scans after a
# 2s sleep so fnconfig reliably catches the pause window and lists visible APs.
patch("fujinet/cmds/fnconfig.as", [
    (
        "                    ldd       #OP_FUJI*256+FUJI$ScanNetworks\n"
        "                    std       request,u\n"
        "                    leax      request,u\n"
        "                    ldy       #2\n"
        "                    lbsr      FBCmd\n"
        "                    lbcs      WifiScanExit\n"
        "                    leax      scanCount,u\n"
        "                    ldy       #1\n"
        "                    lbsr      FBRead\n"
        "                    lbcs      WifiScanExit\n"
        "                    lda       scanCount,u\n"
        "                    cmpa      #MAXNETWORKS\n",
        "                    lda       #6                  retry if STA is mid-reconnect (ESP_ERR_WIFI_STATE)\n"
        "                    sta       workidx,u\n"
        "WifiScanTry@        ldd       #OP_FUJI*256+FUJI$ScanNetworks\n"
        "                    std       request,u\n"
        "                    leax      request,u\n"
        "                    ldy       #2\n"
        "                    lbsr      FBCmd\n"
        "                    lbcs      WifiScanExit\n"
        "                    leax      scanCount,u\n"
        "                    ldy       #1\n"
        "                    lbsr      FBRead\n"
        "                    lbcs      WifiScanExit\n"
        "                    lda       scanCount,u\n"
        "                    bne       WifiScanGot@\n"
        "                    dec       workidx,u\n"
        "                    beq       WifiCountOK\n"
        "                    ldx       #120                2s pause for background reconnect burst to clear\n"
        "                    os9       F$Sleep\n"
        "                    bra       WifiScanTry@\n"
        "WifiScanGot@        cmpa      #MAXNETWORKS\n",
    ),
    (
        "                    leax      request,u\n"
        "                    ldy       #2+SSIDCFGSZ\n"
        "                    lbsr      FBCmd\n"
        "                    bcs       WifiConnectExit\n"
        "                    lbsr      LoadStatus\n"
        "WifiConnectExit     rts\n",
        "                    leax      request,u\n"
        "                    ldy       #2+SSIDCFGSZ\n"
        "                    lbsr      FBCmd\n"
        "                    bcs       WifiConnectExit\n"
        "                    lda       #12                 poll up to ~12s for WPA2 + DHCP IP\n"
        "                    sta       workidx,u\n"
        "WifiWaitIP@         lbsr      LoadStatus\n"
        "                    bcs       WifiConnectExit\n"
        "                    lda       wifistatus,u\n"
        "                    cmpa      #3\n"
        "                    beq       WifiConnectExit\n"
        "                    dec       workidx,u\n"
        "                    beq       WifiConnectExit\n"
        "                    ldx       #60                 1s sleep\n"
        "                    os9       F$Sleep\n"
        "                    bra       WifiWaitIP@\n"
        "WifiConnectExit     rts\n",
    ),
])

patch("games/vi.c", [
    ("0xFF42", "0xFF71"),
])

patch("games/tetris.c", [
    ("0xFF41", "0xFF70"),
    ("0xFF42", "0xFF71"),
    ("on the TFR/911H", "on Centipede 32z"),
])

patch("games/zm.c", [
    ("0xFF41", "0xFF70"),
])

patch("fujinet/c/term.c", [
    ("0xFF42", "0xFF71"),
    ("$FF15", "$FF89"),
])

patch("fujinet/bounce/src/os9/sound.c", [
    ("0xFF41", "0xFF70"),
])

patch("fujinet/battleship/src/os9/sound.c", [
    ("0xFF41", "0xFF70"),
    ("0xFF42", "0xFF71"),
])
PY

mkdir -p "$WORK/build"
if [ -f "$WORK/helpmsg.unix" ]; then
  if [ -f "$SHELF/nitros9/level2/coco3/sys/helpmsg" ]; then
    cat "$SHELF/nitros9/level2/coco3/sys/helpmsg" "$WORK/helpmsg.unix" > "$WORK/build/helpmsg"
  else
    cp "$WORK/helpmsg.unix" "$WORK/build/helpmsg"
  fi
fi

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
make -C "$WORK" -j "$JOBS" NITROS9_DIR="$SHELF/nitros9" FN_APPS="" build/tfr911-level2.dsk

# Verify REL ($130 = 304 bytes) and Boot ($1D0 = 464 bytes) exact sizes so KRN lands at $F000.
REL_SZ=$(wc -c < "$WORK/build/tfr911/level2/rel_80.mod" | tr -d ' ')
BOOT_SZ=$(wc -c < "$WORK/build/tfr911/level2/boot_emu.mod" | tr -d ' ')
if [ "$REL_SZ" != "304" ] || [ "$BOOT_SZ" != "464" ]; then
  echo "error: kernel track alignment mismatch: rel_80.mod=$REL_SZ (want 304), boot_emu.mod=$BOOT_SZ (want 464)" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
cp "$WORK/build/tfr911-level2.dsk" "$OUT"

# Stamp RBF LSN 0 DD.NAM (offset 0x1F, 32 bytes, high-bit terminated) to 'centipede32z-jamesbrown.dsk'.
python3 - "$OUT" << 'PY'
import sys
path = sys.argv[1]
with open(path, "r+b") as f:
    name = bytearray(b"centipede32z-jamesbrown.dsk")
    name[-1] |= 0x80
    field = bytes(name) + b"\x00" * (32 - len(name))
    assert len(field) == 32
    f.seek(0x1F)
    f.write(field)
PY

os9 copy -r "$BASIC09_MOD" "$OUT,CMDS/basic09"
os9 attr -q -r -e -pr -pe -nw -npw -ns "$OUT,CMDS/basic09"

# Build and install the 5 CoCo-safe CMOC C FujiNet apps (fnnews, fnweather, fnstocks, fnbounce, fnbattleship)
FN_APPS_32Z="$V5/build/fn-apps-32z"
FN_SRC_DIR="$WORK/fujinet" sh "$V5/scripts/build-jamesbrown-fn-apps.sh" "$FN_APPS_32Z"
for app in fnnews fnweather fnstocks fnbounce fnbattleship; do
  os9 copy -r "$FN_APPS_32Z/bin/$app" "$OUT,CMDS/$app"
  os9 attr -q -r -w -e -pr -pe "$OUT,CMDS/$app"
done

echo "built $OUT (Motorola 6809E, CoCo-safe I/O map, CMDS/basic09 + fnnews fnweather fnstocks fnbounce fnbattleship)"
