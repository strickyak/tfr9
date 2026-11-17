#!/bin/sh
# make-jamesbrown-32z-dsk.sh -- build CoCo-2 / CoCo-3 I/O-safe Motorola 6809E
# build/centipede32z-jamesbrown.dsk for run_centipede32z_jamesbrown.sh.
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
# transaction, so a previous timed-out network request cannot shift subsequent replies.
patch("fujinet/modules/scdwv.asm", [
    (
        "                    orcc      #IntMasks           freeze the wire for the whole exchange\n"
        "                    ldx       3,s                 system buffer\n",
        "                    orcc      #IntMasks           freeze the wire for the whole exchange\n"
        "FujiDrn@            ldb       >$FF41\n"
        "                    bitb      #$02\n"
        "                    beq       FujiDrnOk@\n"
        "                    ldb       >$FF42\n"
        "                    bra       FujiDrn@\n"
        "FujiDrnOk@          ldx       3,s                 system buffer\n",
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

echo "built $OUT (Motorola 6809E, CoCo-safe I/O map, CMDS/basic09 attr r e pr pe)"
