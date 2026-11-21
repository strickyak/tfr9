#!/bin/sh
# diagnose_rp2350b.sh [--tfr911h | --centipede | --wire=DEV | --all] [SUBCOMMAND ...]
#
# Non-destructive 48-GPIO electrical, clock, bus, and solder-bridge diagnostic
# for RP2350B boards (TFR/911H and Centipede 32z), supporting both:
#   - Populated boards   (TFR/911H with 63C09E/6809E in socket; Centipede 32z in powered CoCo)
#   - Unpopulated boards (fresh out of the box on USB: empty 63C09E socket or unplugged Centipede)
#
# How it works (via the `rp2350b` Tcl command in firmware/lib/devices/rp2350b_diag.h):
#   1. Captures 1024 simultaneous 48-pin snapshots at ~35 MSa/s and runs a >50 MSa/s
#      edge counter to measure E/Q clock frequencies (0.895 - 3.0 MHz) and quadrature phase.
#   2. Tests every non-toggling GPIO 0..47 with internal ~50k pull-down / pull-up AND
#      ultra-short (~1 cycle @ 150 MHz = ~6.7-8 ns) HIGH and LOW output impulses to
#      distinguish stiff external CMOS/Schmitt drivers (<100 ohm), external pull resistors
#      (1k-33k), weak pulls (~45k-200k), LED loads, and true floating High-Z pins.
#   3. Scans all verified High-Z pins for pin-to-pin QFN-80 / header solder bridges.
#   4. When safe (non-Centipede GPIO 21/22 and High-Z E/Q on GPIO 29/30), clocks E/Q across
#      RESET# on TFR/911H to verify whether a 63x09E/6809E is installed, driving all 16
#      address lines (A0..A15) and fetching the $FFFE:$FFFF reset vector.
#
# Examples:
#   make check-rp2350b
#   sh scripts/diagnose_rp2350b.sh
#   sh scripts/diagnose_rp2350b.sh --tfr911h
#   sh scripts/diagnose_rp2350b.sh --centipede
#   sh scripts/diagnose_rp2350b.sh --wire=/dev/ttyACM0
#   sh scripts/diagnose_rp2350b.sh shorts
#   sh scripts/diagnose_rp2350b.sh clocks
#   sh scripts/diagnose_rp2350b.sh pin 21
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
WIRE=${RP2350_DEV:-${WIRE:-}}
BOARD_QUERY=""
SCAN_ALL=auto

while [ "$#" -gt 0 ]; do
  case "$1" in
    --tfr911h|--tfr9)
      BOARD_QUERY="b=tfr9"
      SCAN_ALL=no
      shift
      ;;
    --centipede|--centipede32z)
      BOARD_QUERY="b=centipede"
      SCAN_ALL=no
      shift
      ;;
    --wire=*)
      WIRE=${1#--wire=}
      SCAN_ALL=no
      shift
      ;;
    --all)
      SCAN_ALL=yes
      shift
      ;;
    --help|-h)
      sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

TETHER=$("$V5/scripts/find-tether.sh") || exit 2

WIRES=""
if [ -n "$WIRE" ]; then
  WIRES=$WIRE
elif [ -n "$BOARD_QUERY" ]; then
  # shellcheck disable=SC2086
  WIRES=$(sh "$V5/scripts/find-board.sh" $BOARD_QUERY) || {
    echo "diagnose_rp2350b: no unlocked board matching '$BOARD_QUERY' found" >&2
    exit 1
  }
else
  # Discover all unlocked RP2350 USB CDC ports (works even on fresh unlabeled boards!).
  WIRES=$(TETHER="$TETHER" python3 - << 'PY'
import glob, os, subprocess

tether = os.environ["TETHER"]
cands = (
    sorted(glob.glob("/dev/serial/by-id/*Pico*"))
    + sorted(glob.glob("/dev/serial/by-id/*RP2350*"))
    + sorted(glob.glob("/dev/ttyACM*"))
    + sorted(glob.glob("/dev/cu.usbmodem*"))
)
seen = set()
unlocked = []
for c in cands:
    if not os.path.exists(c):
        continue
    real = os.path.realpath(c)
    if real in seen:
        continue
    seen.add(real)
    try:
        r = subprocess.run(
            [tether, "-n", "-bind=", f"-wire={c}", "-just-ping=1"],
            capture_output=True, text=True, timeout=4
        )
        if r.returncode == 0 and "OK" in r.stdout:
            unlocked.append(c)
    except subprocess.TimeoutExpired:
        pass
for u in unlocked:
    print(u)
PY
  )
  if [ -z "$WIRES" ]; then
    echo "diagnose_rp2350b: no unlocked RP2350B board found on USB (is tether already open on it?)" >&2
    exit 1
  fi
  if [ "$SCAN_ALL" = "no" ]; then
    WIRES=$(printf '%s\n' "$WIRES" | head -n 1)
  fi
fi

SUBCMD=${1:-diag}
OVERALL_RC=0

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/rp2350b-diag.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

idx_num=0
for dev in $WIRES; do
  idx_num=$((idx_num + 1))
  if [ "$idx_num" -gt 1 ]; then
    echo ""
  fi
  echo "=== Probing RP2350B on $dev ===" >&2

  # Enter Phase 1 (TCL>) cleanly so PIO bus engines are idle before probing GPIOs:
  "$TETHER" -n -bind="" -wire="$dev" -just-restart=27 >/dev/null 2>&1 || true
  sleep 2

  if [ "$SUBCMD" != "diag" ]; then
    CMD="rp2350b $*"
    "$TETHER" -n -bind="" -wire="$dev" -just-inject="$CMD" || OVERALL_RC=1
    "$TETHER" -n -bind="" -wire="$dev" -just-restart=27 >/dev/null 2>&1 || true
    continue
  fi

  LABEL_OUT=$("$TETHER" -n -bind="" -wire="$dev" -just-get-label 2>/dev/null || true)
  RAW_FILE="$TMP_DIR/raw_$idx_num.txt"
  inj_rc=0
  "$TETHER" -n -bind="" -wire="$dev" -just-inject="rp2350b raw" >"$RAW_FILE" 2>&1 || inj_rc=$?

  # Restore fresh Phase 1 state (resynchronizes Centipede PMODE4 spoonfeeder if CoCo is powered on):
  "$TETHER" -n -bind="" -wire="$dev" -just-restart=27 >/dev/null 2>&1 || true

  DEV_RC=0
  DEV_PATH="$dev" LABEL_RAW="$LABEL_OUT" INJ_RC="$inj_rc" python3 - "$RAW_FILE" << 'PY' || DEV_RC=$?
import os, re, sys

raw_text = open(sys.argv[1], errors="replace").read()
dev_path = os.environ["DEV_PATH"]
label_raw = os.environ.get("LABEL_RAW", "").strip()
inj_rc = int(os.environ.get("INJ_RC", "0"))

# Parse flash label if present (`p=1,...`)
label_line = ""
for ln in label_raw.splitlines():
    if ln.startswith("p=1,"):
        label_line = ln.strip()
        break

if inj_rc != 0 or "META fw=" not in raw_text:
    print(f"ERROR: failed to run 'rp2350b raw' on {dev_path} (does this board have current v5 firmware?)")
    if "unknown command" in raw_text or "invalid command name" in raw_text:
        print("  Hint: reflash the board with 'make tfr911h' or 'make centipede32z' so it has the 'rp2350b' Tcl command.")
    else:
        print(raw_text[-600:])
    sys.exit(1)

meta = {}
m_meta = re.search(
    r"META fw[=](\S+) id[=](\S+) sys_mhz[=](\d+) burst_us[=](\d+) eq_q[=](\d+),(\d+),(\d+),(\d+) eq_seq[=](\d+),(\d+),(\d+)",
    raw_text,
)
if m_meta:
    meta = {
        "fw": m_meta.group(1),
        "id": m_meta.group(2),
        "sys_mhz": int(m_meta.group(3)),
        "burst_us": max(1, int(m_meta.group(4))),
        "eq_q": [int(m_meta.group(i)) for i in range(5, 9)],
        "eq_seq": [int(m_meta.group(i)) for i in range(9, 12)],
    }

pins = {}
for m in re.finditer(
    r"PIN (\d+) (\S+)\s+pd[=](\d+) pu[=](\d+) lf[=](\d+) lh[=](\d+) hf[=](\d+) hh[=](\d+) edges[=](\d+) hz[=](\d+) duty[=](\d+)",
    raw_text,
):
    g = int(m.group(1))
    pins[g] = {
        "cls": m.group(2),
        "pd": int(m.group(3)),
        "pu": int(m.group(4)),
        "lf": int(m.group(5)),
        "lh": int(m.group(6)),
        "hf": int(m.group(7)),
        "hh": int(m.group(8)),
        "edges": int(m.group(9)),
        "hz": int(m.group(10)),
        "duty": int(m.group(11)),
    }

shorts = []
m_sh = re.search(r"SHORT count[=](\d+)(.*)", raw_text)
if m_sh and int(m_sh.group(1)) > 0:
    for pair in m_sh.group(2).strip().split():
        if "-" in pair:
            a, b = pair.split("-", 1)
            shorts.append((int(a), int(b)))

cpu = {"ran": False, "drv_abus": 0, "rst_abus": 0, "ctrl_drv": 0, "ctrl_val": 0, "fffe": False, "ffff": False}
m_cpu = re.search(
    r"CPU ran[=](\d+) drv_abus[=]([0-9A-Fa-f]+) rst_abus[=]([0-9A-Fa-f]+) ctrl_drv[=]([0-9A-Fa-f]+) ctrl_val[=]([0-9A-Fa-f]+) fffe[=](\d+) ffff[=](\d+)",
    raw_text,
)
if m_cpu:
    cpu = {
        "ran": int(m_cpu.group(1)) != 0,
        "drv_abus": int(m_cpu.group(2), 16),
        "rst_abus": int(m_cpu.group(3), 16),
        "ctrl_drv": int(m_cpu.group(4), 16),
        "ctrl_val": int(m_cpu.group(5), 16),
        "fffe": int(m_cpu.group(6)) != 0,
        "ffff": int(m_cpu.group(7)) != 0,
    }

# Pin role tables for both boards:
TFR_ROLES = {
    **{i: (f"D{i}", "63x09E Data Bus") for i in range(8)},
    8:  ("AUDIO/H8", "SN76489 PWM Audio / J1-1 (40c R2 NC)"),
    9:  ("H9",       "J1-3 (40c R1 NC)"),
    10: ("H10CD",    "J1-5 Hat PIN_CD (IO22) / Orch90-L"),
    11: ("H11",      "J1-7 Hat NC / Orch90-R"),
    12: ("UART0_TX", "J1-9 -> Hat ESP32 IO33 (RX)"),
    13: ("UART0_RX", "J1-11 <- Hat ESP32 IO21 (TX)"),
    14: ("H14",      "J1-13 <- Hat ESP32 IO26 (CASS_OUT)"),
    15: ("H15",      "J1-15 <- Hat ESP32 IO25 (DAC1)"),
    16: ("H16",      "J1-17 Expansion J4-1"),
    17: ("H17",      "J1-19 Expansion J4-3"),
    18: ("H18",      "J1-20 Expansion J4-5"),
    19: ("H19",      "J1-18 Expansion J4-7"),
    20: ("RESET#",   "63x09E _RESET (open-drain)"),
    21: ("NMI#",     "63x09E _NMI (open-drain)"),
    22: ("IRQ#",     "63x09E _IRQ (open-drain)"),
    23: ("FIRQ#",    "63x09E _FIRQ (open-drain)"),
    24: ("HALT#",    "63x09E _HALT (open-drain)"),
    25: ("LED",      "On-board Status LED"),
    26: ("LIC",      "63x09E Last Instruction Cycle (CPU out)"),
    27: ("AVMA",     "63x09E Advanced Valid Mem Addr (CPU out)"),
    28: ("BS",       "63x09E Bus Status (CPU out)"),
    29: ("E",        "63x09E E Clock (RP2350B out -> CPU in)"),
    30: ("Q",        "63x09E Q Clock (RP2350B out -> CPU in)"),
    31: ("R/W",      "63x09E Read/Write (CPU out)"),
    **{32 + i: (f"A{i}", f"63x09E Address A{i}") for i in range(16)},
}

CENT_ROLES = {
    **{i: (f"D{i}", "CoCo Cart Data Bus") for i in range(8)},
    8:  ("G_CTS#",   "CoCo CTS# ($C000-$FEFF select, CoCo out)"),
    9:  ("G_SCS#",   "CoCo SCS# ($FF40-$FF5F select, CoCo out)"),
    10: ("H10/OR_L", "J1-5 Hat PIN_CD (IO22) / Orch90-L"),
    11: ("ORCH_R",   "Orchestra-90 Right PWM"),
    12: ("UART0_TX", "J1-9 -> Hat ESP32 IO33 (RX)"),
    13: ("UART0_RX", "J1-11 <- Hat ESP32 IO21 (TX)"),
    14: ("H14",      "J1-13 <- Hat ESP32 IO26 (CASS_OUT)"),
    15: ("H15",      "J1-15 <- Hat ESP32 IO25 (DAC1)"),
    16: ("H16",      "J1-17 Expansion J4-1"),
    17: ("H17",      "J1-19 Expansion J4-3"),
    18: ("H18",      "J1-20 Expansion J4-5"),
    19: ("H19",      "J1-18 Expansion J4-7"),
    20: ("G_RW",     "CoCo 6809E R/W (CoCo out)"),
    21: ("G_EBAR",   "Schmitt Inverter <- CoCo E Clock"),
    22: ("G_QBAR",   "Schmitt Inverter <- CoCo Q Clock"),
    23: ("GP23",     "Unused / internal"),
    24: ("GP24",     "Unused / internal"),
    25: ("G_LED",    "On-board Status LED"),
    26: ("G_SND",    "CoCo Cart SND Audio"),
    27: ("G_CART",   "CoCo Cart CART# (PIA1 CB1 FIRQ)"),
    28: ("G_SLENB#", "CoCo Cart SLENB# (bus disable)"),
    29: ("G_HALT#",  "CoCo 6809E HALT# (open-drain)"),
    30: ("G_NMI#",   "CoCo 6809E NMI# (open-drain)"),
    31: ("G_RESET#", "CoCo RESET# (open-drain / input)"),
    **{32 + i: (f"A{i}", f"CoCo Cart Address A{i}") for i in range(16)},
}

p21 = pins.get(21, {})
p22 = pins.get(22, {})
f21 = p21.get("hz", 0)
f22 = p22.get("hz", 0)

# Independent electrical board & population classification:
eq_oscillating = (300_000 <= f21 <= 4_500_000) and (300_000 <= f22 <= 4_500_000)
eq_schmitt_dc = (
    not eq_oscillating
    and p21.get("cls") in ("DRV_HIGH", "DRV_LOW")
    and p22.get("cls") in ("DRV_HIGH", "DRV_LOW")
)

inferred_board = "unknown"
population_state = "unknown"
coco_speed_desc = ""

if eq_oscillating:
    inferred_board = "centipede32z"
    population_state = "POPULATED (plugged into powered CoCo)"
    mhz = (f21 + f22) / 2_000_000.0
    if 0.80 <= mhz <= 1.00:
        coco_speed_desc = f"{mhz:.3f} MHz (standard 0.895 MHz CoCo E/Q)"
    elif 1.65 <= mhz <= 1.95:
        coco_speed_desc = f"{mhz:.3f} MHz (double-speed 1.790 MHz CoCo E/Q)"
    elif 2.60 <= mhz <= 3.10:
        coco_speed_desc = f"{mhz:.3f} MHz (2.86 MHz high-speed E/Q)"
    else:
        coco_speed_desc = f"{mhz:.3f} MHz"
elif eq_schmitt_dc:
    inferred_board = "centipede32z"
    population_state = (
        f"UNPOPULATED / UNPLUGGED (CoCo absent; Schmitt triggers stiffly driving "
        f"GP21={p21['cls']}, GP22={p22['cls']})"
    )
elif cpu["ran"]:
    inferred_board = "tfr911h"
    if cpu["drv_abus"] == 0xFFFF and (cpu["ctrl_drv"] & 0xC) == 0xC:
        population_state = "POPULATED (63x09E / 6809E CPU installed in 40-pin socket)"
    elif cpu["drv_abus"] == 0 and cpu["ctrl_drv"] == 0:
        population_state = "UNPOPULATED (40-pin 63x09E socket empty — fresh out of the box)"
    else:
        population_state = (
            f"PARTIALLY POPULATED / SOCKET FAULT "
            f"(drv_abus=0x{cpu['drv_abus']:04X}, ctrl_drv=0x{cpu['ctrl_drv']:X})"
        )
else:
    # Fall back to firmware hint if electrical state is ambiguous:
    inferred_board = meta.get("fw", "tfr911h")
    population_state = "AMBIGUOUS (check pin table below)"

roles = CENT_ROLES if inferred_board == "centipede32z" else TFR_ROLES

sys_mhz = meta.get("sys_mhz", 250)
pulse_cyc = 2 if sys_mhz > 160 else 1
pulse_ns = pulse_cyc * (1000.0 / sys_mhz)
msps = 1024.0 / meta.get("burst_us", 28)

print(f"=== RP2350B Hardware & 48-GPIO Diagnostic ===")
print(f"  Serial Port:   {dev_path}")
print(f"  Pico ID:       {meta.get('id', '?')}   Flash Label: {label_line or '(none)'}")
print(f"  Firmware:      {meta.get('fw', '?')}   (clk_sys={sys_mhz} MHz, impulse={pulse_cyc} cyc = {pulse_ns:.1f} ns, burst={msps:.1f} MSa/s)")
print(f"  Inferred PCB:  {inferred_board}")
print(f"  Population:    {population_state}")
if coco_speed_desc:
    q00, q01, q10, q11 = meta.get("eq_q", [0, 0, 0, 0])
    qf, qr, qd = meta.get("eq_seq", [0, 0, 0])
    print(f"  E/Q Clocks:    GP21(EBAR)={f21/1e6:.4f} MHz ({p21['duty']}%), GP22(QBAR)={f22/1e6:.4f} MHz ({p22['duty']}%) — {coco_speed_desc}")
    print(f"  Quadrature:    states 00={q00} 01={q01} 11={q11} 10={q10} | steps fwd={qf} rev={qr} diag={qd}")

# Detect FujiNet hat signature on GPIO 10 (IO22 PIN_CD), 12 (IO33 RX), 13 (IO21 TX):
p10 = pins.get(10, {}).get("cls", "")
p12 = pins.get(12, {}).get("cls", "")
p13 = pins.get(13, {}).get("cls", "")
hat_desc = "not detected (GPIO 10/12/13 High-Z)"
if p13 == "DRV_HIGH" and p12 in ("DRV_HIGH", "PULLUP", "WK_PU"):
    cd_state = "WiFi CD# asserted via R5 1K" if p10 in ("DRV_LOW", "PULLDN") else f"GP10={p10}"
    hat_desc = f"DETECTED (GP12={p12}, GP13=DRV_HIGH <- ESP32 IO21, {cd_state})"
elif p10 in ("PULLUP", "DRV_HIGH", "DRV_LOW", "PULLDN") and p13 == "HI_Z":
    hat_desc = f"POWERED HAT WITH IDLE/UNBODGED TX (GP10={p10}, GP13=HI_Z — check 41d IO13<->IO21 bodge or ESP32 FW)"
print(f"  FujiNet Hat:   {hat_desc}")
if cpu["ran"]:
    if cpu["drv_abus"] == 0 and cpu["ctrl_drv"] == 0:
        print("  63x09E Probe:  socket empty (0/16 addr & 0/4 status lines driven under E/Q clock)")
    else:
        n_addr = bin(cpu["drv_abus"]).count("1")
        n_ctrl = bin(cpu["ctrl_drv"]).count("1")
        vec_ok = "YES ($FFFE -> $FFFF)" if (cpu["fffe"] and cpu["ffff"]) else f"NO ($FFFE={cpu['fffe']}, $FFFF={cpu['ffff']})"
        print(f"  63x09E Probe:  {n_addr}/16 addr + {n_ctrl}/4 ctrl/status driven under E/Q clock; RESET vector fetch: {vec_ok}")
print("")

print("  GPIO  Signal     State      Freq / Pull & Impulse (pd,pu | L:fast->20us | H:fast->20us)  Description")
print("  ----  ---------  ---------  -----------------------------------------------------------  -----------")
for g in range(48):
    d = pins.get(g)
    if not d:
        continue
    sig, desc = roles.get(g, (f"GP{g}", ""))
    if d["hz"] > 0:
        if d["hz"] >= 1_000_000:
            detail = f"{d['hz']/1e6:7.3f} MHz  duty={d['duty']:2d}%  (burst_edges={d['edges']})"
        elif d["hz"] >= 1_000:
            detail = f"{d['hz']/1e3:7.1f} kHz  duty={d['duty']:2d}%  (burst_edges={d['edges']})"
        else:
            detail = f"{d['hz']:7d} Hz   duty={d['duty']:2d}%  (burst_edges={d['edges']})"
    else:
        detail = (
            f"pd={d['pd']} pu={d['pu']}  |  "
            f"L:{d['lf']}->{d['lh']}  H:{d['hf']}->{d['hh']}"
        )
    print(f"   {g:02d}   {sig:<9s}  {d['cls']:<9s}  {detail:<59s}  {desc}")

print("")
problems = []
notes = []

# 1. Check firmware vs inferred board mismatch:
fw = meta.get("fw", "")
if inferred_board in ("tfr911h", "centipede32z") and fw != inferred_board:
    problems.append(
        f"Firmware mismatch: running '{fw}' firmware, but electrical signature is '{inferred_board}'!"
    )

# 2. Check flash label vs inferred board mismatch:
if "b=tfr9" in label_line and inferred_board == "centipede32z":
    problems.append(f"Flash label says '{label_line}', but electrical signature is Centipede 32z!")
if "b=centipede" in label_line and inferred_board == "tfr911h":
    problems.append(f"Flash label says '{label_line}', but electrical signature is TFR/911H!")

# 3. Check HI_Z pin-to-pin shorts:
if shorts:
    for a, b in shorts:
        sa = roles.get(a, (f"GP{a}", ""))[0]
        sb = roles.get(b, (f"GP{b}", ""))[0]
        problems.append(f"Pin-to-pin short/bridge detected: GPIO {a} ({sa}) <-> GPIO {b} ({sb})")

# 4. Board-specific health checks:
if inferred_board == "centipede32z":
    if eq_oscillating:
        # Check quadrature health:
        qf, qr, qd = meta.get("eq_seq", [0, 0, 0])
        if (qf + qr) < 10:
            problems.append("GP21/GP22 clocks active but quadrature transitions missing")
        if abs(f21 - f22) > max(f21, f22) * 0.05:
            problems.append(f"EBAR ({f21} Hz) and QBAR ({f22} Hz) frequencies differ by >5%!")
        # Check CoCo cartridge bus presence on CTS#, SCS#, RW, A0..A15:
        for cp, cname in [(8, "G_CTS#"), (9, "G_SCS#")]:
            if pins[cp]["cls"] == "HI_Z":
                problems.append(f"GPIO {cp} ({cname}) is HI_Z while CoCo E/Q clocks are running (cartridge finger dirty/open?)")
        if pins[20]["cls"] == "HI_Z":
            problems.append("GPIO 20 (G_RW) is HI_Z while CoCo E/Q clocks are running (cartridge finger open?)")
        hiz_addr = [g - 32 for g in range(32, 48) if pins[g]["cls"] == "HI_Z"]
        if hiz_addr:
            astr = ", ".join(f"A{i}(GP{32+i})" for i in hiz_addr)
            problems.append(f"Floating address pin(s) while CoCo is running: {astr}")
    else:
        # Unplugged Centipede: cartridge bus fingers should NOT be stuck DRV_HIGH / DRV_LOW
        stuck = [
            f"GP{g}({roles[g][0]}={pins[g]['cls']})"
            for g in list(range(0, 10)) + [20] + list(range(26, 48))
            if pins[g]["cls"] in ("DRV_HIGH", "DRV_LOW")
        ]
        if stuck:
            problems.append("Unexpected driven cartridge pin(s) while unplugged from CoCo: " + ", ".join(stuck))
elif inferred_board == "tfr911h":
    if cpu["ran"]:
        if cpu["drv_abus"] == 0 and cpu["ctrl_drv"] == 0:
            notes.append("Unpopulated TFR/911H: 40-pin 63x09E socket is empty and all bus GPIOs are clean High-Z.")
            # On unpopulated TFR/911H, verify no bus pin is stuck DRV_LOW or DRV_HIGH (ignoring hat pins 8..19 and LED 25):
            bus_gpios = list(range(0, 8)) + list(range(20, 25)) + list(range(26, 48))
            stuck = [f"GP{g}({roles[g][0]}={pins[g]['cls']})" for g in bus_gpios if pins[g]["cls"] in ("DRV_HIGH", "DRV_LOW")]
            if stuck:
                problems.append("Stuck bus pin(s) on unpopulated TFR/911H: " + ", ".join(stuck))
        else:
            missing_a = [i for i in range(16) if not (cpu["drv_abus"] & (1 << i))]
            if missing_a:
                astr = ", ".join(f"A{i}(GP{32+i})" for i in missing_a)
                problems.append(f"63x09E did not drive address pin(s) during RESET: {astr}")
            for bit, g, sname in [(0, 26, "LIC"), (1, 27, "AVMA"), (2, 28, "BS"), (3, 31, "R/W")]:
                if not (cpu["ctrl_drv"] & (1 << bit)):
                    problems.append(f"63x09E did not drive {sname} (GPIO {g}) during RESET")
            if not (cpu["fffe"] and cpu["ffff"]):
                problems.append(
                    f"63x09E did not perform BS=1 reset vector fetch $FFFE->$FFFF after releasing RESET# "
                    f"(saw $FFFE={cpu['fffe']}, $FFFF={cpu['ffff']})"
                )
            else:
                notes.append(
                    "63x09E clocked RESET test PASSED: all 16 address lines + LIC/AVMA/BS/RW actively driven, "
                    "$FFFE->$FFFF vector fetch verified! (Note: unclocked 63C09E holds A0..A15 & R/W Hi-Z before clocking.)"
                )
    if pins[8]["cls"] in ("DRV_HIGH", "DRV_LOW"):
        problems.append(
            f"TFR/911H GPIO 8 (AUDIO/H8) is {pins[8]['cls']} — unbodged hat-fuji-40c R2 conflicts with SN76489 PWM audio!"
        )

rule = "=" * 76
print(rule)
if not problems:
    print(f"RP2350B VERDICT: PASS — {inferred_board} ({population_state})")
    print(f"  - HI_Z Shorts:  0 bridges detected across floating GPIOs")
    if coco_speed_desc:
        print(f"  - CoCo Clocks:  {coco_speed_desc}, quadrature OK")
    for nt in notes:
        print(f"  - Note:         {nt}")
    print(rule)
    sys.exit(0)
else:
    print(f"RP2350B VERDICT: FAIL — {inferred_board} ({population_state})")
    print(f"Problems discovered ({len(problems)}):")
    for i, p in enumerate(problems, 1):
        print(f"  {i}. {p}")
    for nt in notes:
        print(f"  Note: {nt}")
    print(rule)
    sys.exit(1)
PY
  if [ "$DEV_RC" -ne 0 ]; then
    OVERALL_RC=1
  fi
done

exit "$OVERALL_RC"
