#!/bin/sh
# diagnose_fujinet.sh [--centipede | --wire=DEV] [--esp-port=DEV | --no-esp-usb] [SUBCOMMAND ...]
#
# Full hardware & protocol diagnostic for a FujiNet hat on an attached TFR/911H
# or Centipede 32z (auto-detected unless --tfr911h / --centipede / --wire is given):
#   1. If an ESP32 USB-UART cable (/dev/ttyUSB* or --esp-port=DEV) is ALSO plugged
#      in (and no specific subcommand other than `diag` or `matrix` was given),
#      holds the ESP32 in ROM bootloader (all ESP32 pins verified hi-Z) and sweeps
#      every TFR/911H hat pin (GPIO 8..19) 0 -> 1 -> Z while reading ESP32
#      GPIO_IN_REG / GPIO_IN1_REG directly via esptool:
#        - Verifies GPIO 8/9 NC (R1/R2 absent), GPIO 10 <-> IO22, GPIO 12 <-> IO33,
#          GPIO 13 <-> IO21 (40c+bodges) OR IO13+IO21 (41d + IO13<->IO21 bodge),
#          GPIO 14 <-> IO26, GPIO 15 <-> IO25, GPIO 16..19 unshorted, plus PCB
#          pull-ups on IO14 (R7), IO36 (R14), IO39 (R15).
#        - Detects open solder joints, missing 41d IO13<->IO21 bodge, and pin-to-pin
#          shorts/bridges without flashing any custom diagnostic firmware.
#   2. Hard-resets the ESP32 into its normal FujiNet firmware and runs `fujinet diag`
#      from the TFR/911H (electrical pin states + 921600-baud UART scan + OP_TIME +
#      AdapterConfig + MAC + WiFi + Host/Device slots).
#   3. Prints a final JAMESBROWN VERDICT stating whether the hat is expected to work
#      with `jamesbrown` (`run_tfr911h_jamesbrown.sh` / `run_centipede32z_jamesbrown.sh`),
#      or listing every problem found.
#
# Examples:
#   make check-fujinet
#   sh scripts/diagnose_fujinet.sh
#   sh scripts/diagnose_fujinet.sh matrix
#   sh scripts/diagnose_fujinet.sh --no-esp-usb
#   sh scripts/diagnose_fujinet.sh pins
#   sh scripts/diagnose_fujinet.sh status
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
WIRE=${TFR_DEV:-${WIRE:-}}
ESP_PORT=${FUJINET_PORT:-}
USE_ESP_USB=auto
BOARD_QUERY=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --centipede)
      BOARD_QUERY="b=centipede"
      shift
      ;;
    --tfr911h)
      BOARD_QUERY="b=tfr9 v=911h"
      shift
      ;;
    --wire=*)
      WIRE=${1#--wire=}
      shift
      ;;
    --esp-port=*)
      ESP_PORT=${1#--esp-port=}
      USE_ESP_USB=yes
      shift
      ;;
    --no-esp-usb)
      USE_ESP_USB=no
      shift
      ;;
    --help|-h)
      sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

TETHER=$("$V5/scripts/find-tether.sh") || exit 2

if [ -z "$WIRE" ]; then
  if [ -n "$BOARD_QUERY" ]; then
    # shellcheck disable=SC2086
    WIRE=$(sh "$V5/scripts/find-board.sh" $BOARD_QUERY) || {
      echo "diagnose_fujinet: no board matching '$BOARD_QUERY' found" >&2
      exit 1
    }
  else
    WIRE=$(sh "$V5/scripts/find-board.sh" b=tfr9 v=911h 2>/dev/null || sh "$V5/scripts/find-board.sh" b=centipede v=32z 2>/dev/null || true)
    if [ -z "$WIRE" ]; then
      echo "diagnose_fujinet: no TFR/911H or Centipede 32z board found" >&2
      exit 1
    fi
  fi
fi

SUBCMD=${1:-diag}

# Auto-detect single ESP32 USB-UART port when running full `diag` or `matrix`.
if [ "$USE_ESP_USB" != "no" ] && { [ "$SUBCMD" = "diag" ] || [ "$SUBCMD" = "matrix" ]; }; then
  if [ -z "$ESP_PORT" ]; then
    esp_count=0
    for dev in /dev/ttyUSB* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial*; do
      if [ -e "$dev" ]; then
        ESP_PORT=$dev
        esp_count=$((esp_count + 1))
      fi
    done
    if [ "$esp_count" -ne 1 ]; then
      ESP_PORT=""
      if [ "$SUBCMD" = "matrix" ]; then
        echo "diagnose_fujinet: 'matrix' requires exactly 1 ESP32 USB-UART port (found $esp_count); pass --esp-port=/dev/..." >&2
        exit 1
      fi
    fi
  fi
fi

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/fujinet-diag.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM
MATRIX_JSON="$TMP_DIR/matrix.json"
DIAG_TXT="$TMP_DIR/diag.txt"

echo "=== Host Board: $WIRE ===" >&2
"$TETHER" -n -bind="" -wire="$WIRE" -just-restart=27 >/dev/null 2>&1 || true
sleep 2

matrix_rc=0
if [ -n "$ESP_PORT" ] && { [ "$SUBCMD" = "diag" ] || [ "$SUBCMD" = "matrix" ]; }; then
  echo "=== ESP32 USB Port: $ESP_PORT ===" >&2
  TETHER="$TETHER" WIRE="$WIRE" ESP_PORT="$ESP_PORT" MATRIX_JSON="$MATRIX_JSON" python3 - << 'PY' || matrix_rc=$?
import json, os, subprocess, sys, time

esptool_dir = os.path.expanduser("~/.platformio/packages/tool-esptoolpy")
if os.path.isdir(esptool_dir):
    sys.path.insert(0, esptool_dir)

try:
    import esptool
except ImportError as exc:
    print(f"WARN: python esptool module unavailable ({exc}); skipping USB continuity matrix.", file=sys.stderr)
    sys.exit(0)

tether = os.environ["TETHER"]
wire = os.environ["WIRE"]
esp_port = os.environ["ESP_PORT"]
matrix_json = os.environ["MATRIX_JSON"]

def tfr_inject(cmd: str) -> str:
    res = subprocess.run(
        [tether, "-n", "-bind=", f"-wire={wire}", f"-just-inject={cmd}"],
        capture_output=True, text=True, check=False,
    )
    if res.returncode != 0:
        raise RuntimeError(f"tether just-inject failed ({cmd}): {res.stderr.strip()}")
    return res.stdout.strip()

IO_MUX = {
    12: 0x3FF49034,
    13: 0x3FF49038,
    14: 0x3FF49030,
    15: 0x3FF4903C,
    16: 0x3FF4904C,
    17: 0x3FF49050,
    21: 0x3FF4907C,
    22: 0x3FF49080,
    25: 0x3FF49024,
    26: 0x3FF49028,
    27: 0x3FF4902C,
    32: 0x3FF4901C,
    33: 0x3FF49020,
    34: 0x3FF49014,
    35: 0x3FF49018,
    36: 0x3FF49004,
    39: 0x3FF49010,
}

GPIO_ENABLE_REG = 0x3FF44020
GPIO_ENABLE1_REG = 0x3FF4402C
GPIO_IN_REG = 0x3FF4403C
GPIO_IN1_REG = 0x3FF44040

tfr_inject("foreach g {8 9 10 11 12 13 14 15 16 17 18 19} {fujinet drive $g z}")

esp = esptool.targets.ESP32ROM(esp_port, 115200)
esp.connect("default_reset")
mac = ":".join(f"{b:02X}" for b in esp.read_mac())
chip = esp.get_chip_description()

en0 = esp.read_reg(GPIO_ENABLE_REG)
en1 = esp.read_reg(GPIO_ENABLE1_REG) & 0xFF
io13_hiz = ((en0 >> 13) & 1) == 0

for pin, reg in IO_MUX.items():
    esp.write_reg(reg, 0x2200)

def read_esp_pins():
    in0 = esp.read_reg(GPIO_IN_REG)
    in1 = esp.read_reg(GPIO_IN1_REG)
    vals = {}
    for pin in IO_MUX:
        if pin < 32:
            vals[pin] = (in0 >> pin) & 1
        else:
            vals[pin] = (in1 >> (pin - 32)) & 1
    return vals

base = read_esp_pins()
print("=== ESP32 Bootloader Continuity, Bodge & High-Z Matrix ===")
print(f"  ESP32 Chip: {chip}   MAC: {mac}")
print(f"  Bootloader GPIO_ENABLE: 0x{en0:08X} / 0x{en1:02X}  (IO13 hi-Z: {'YES' if io13_hiz else 'NO!'})")
print(
    f"  PCB Pull-Ups: IO14(R7/SAFE_RESET)={base[14]}  "
    f"IO36(R14/A14)={base[36]}  IO39(R15/A15)={base[39]}  IO15(SD_CD)={base[15]}"
)

HAT_SPEC = [
    (8,  "J1-1  H8   ", "NC (40c R2 removed)"),
    (9,  "J1-3  H9   ", "NC (40c R1 removed)"),
    (10, "J1-5  H10CD", "IO22 (R5 1K PIN_CD)"),
    (11, "J1-7  H11  ", "NC"),
    (12, "J1-9  H12  ", "IO33 (UART1_RX / CASS_IN)"),
    (13, "J1-11 H13  ", "IO21 (40c-bodged) or IO13+IO21 (41d+bodge)"),
    (14, "J1-13 H14  ", "IO26 (CASS_OUT)"),
    (15, "J1-15 H15  ", "IO25 (DAC1)"),
    (16, "J1-17 H16  ", "NC (J4-1)"),
    (17, "J1-19 H17  ", "NC (J4-3)"),
    (18, "J1-20 H18  ", "NC (J4-5)"),
    (19, "J1-18 H19  ", "NC (J4-7)"),
]

failures = []
if not io13_hiz:
    failures.append("ESP32 IO13 is not high-impedance (GPIO_ENABLE bit 13 set)")
for pu_pin, pu_name in [(14, "IO14/R7 (SAFE_RESET)"), (36, "IO36/R14 (A14)"), (39, "IO39/R15 (A15)")]:
    if base[pu_pin] != 1:
        failures.append(f"PCB pull-up {pu_name} read 0 (expected 1)")

hat_variant = "unknown"
try:
    for gpio, label, expected_desc in HAT_SPEC:
        tfr_inject(f"fujinet drive {gpio} 0")
        time.sleep(0.01)
        low_vals = read_esp_pins()
        tfr_inject(f"fujinet drive {gpio} 1")
        time.sleep(0.01)
        high_vals = read_esp_pins()
        tfr_inject(f"fujinet drive {gpio} z")

        toggled = sorted(p for p in IO_MUX if low_vals[p] == 0 and high_vals[p] == 1)
        inverted = sorted(p for p in IO_MUX if low_vals[p] == 1 and high_vals[p] == 0)
        hits_str = ",".join(f"IO{p}" for p in toggled) if toggled else "none"
        if inverted:
            hits_str += " (inv:" + ",".join(f"IO{p}" for p in inverted) + ")"

        verdict = "PASS"
        if inverted:
            verdict = f"FAIL (inverted response on {hits_str})"
        elif gpio in (8, 9, 11, 16, 17, 18, 19):
            if toggled:
                if gpio == 8 and toggled == [21]:
                    verdict = "FAIL (unbodged 40c R2 still installed -> IO21; conflicts with GPIO 8 PWM audio!)"
                elif gpio == 9 and toggled == [33]:
                    verdict = "FAIL (unbodged 40c R1 still installed -> IO33!)"
                else:
                    verdict = f"FAIL (unexpected short/bridge to {hits_str})"
        elif gpio == 10:
            if toggled != [22]:
                verdict = f"FAIL (expected IO22 via R5, got {hits_str})"
        elif gpio == 12:
            if toggled != [33]:
                verdict = f"FAIL (expected IO33 UART1_RX, got {hits_str})"
        elif gpio == 13:
            if toggled == [13, 21]:
                verdict = "PASS (41d + IO13<->IO21 bodge verified!)"
                hat_variant = "hat-fuji-41d + IO13<->IO21 bodge"
            elif toggled == [21]:
                verdict = "PASS (40c + bodges -> IO21)"
                hat_variant = "hat-fuji-40c + 6 bodges"
            elif toggled == [13]:
                verdict = "FAIL (41d UNBODGED: GPIO 13 reaches IO13 only, missing IO13<->IO21 bodge!)"
                hat_variant = "hat-fuji-41d (UNBODGED)"
            else:
                verdict = f"FAIL (expected IO21 or IO13+IO21, got {hits_str})"
        elif gpio == 14:
            if toggled != [26]:
                verdict = f"FAIL (expected IO26 CASS_OUT, got {hits_str})"
        elif gpio == 15:
            if toggled != [25]:
                verdict = f"FAIL (expected IO25 DAC1, got {hits_str})"

        if verdict.startswith("FAIL"):
            failures.append(f"TFR GPIO {gpio} ({label.strip()}): {verdict}")
        print(f"  TFR GPIO {gpio:2d} ({label}) -> ESP32 {hits_str:<12} [{verdict}]  ({expected_desc})")
finally:
    tfr_inject("foreach g {8 9 10 11 12 13 14 15 16 17 18 19} {fujinet drive $g z}")
    esp.hard_reset()
    esp._port.close()

with open(matrix_json, "w") as jf:
    json.dump({
        "ran": True,
        "mac": mac,
        "chip": chip,
        "hat_variant": hat_variant,
        "sd_inserted": base[15] == 0,
        "failures": failures,
    }, jf)

if failures:
    print(f"Matrix summary: FAIL ({len(failures)} issue(s))")
    sys.exit(1)
else:
    print(f"Matrix summary: ALL 12 HAT PINS & PULL-UPS PASSED ({hat_variant})!")
PY
  if [ "$SUBCMD" = "matrix" ]; then
    exit "$matrix_rc"
  fi
  sleep 2
fi

if [ "$#" -eq 0 ]; then
  CMD="fujinet diag"
else
  CMD="fujinet $*"
fi

if [ "$SUBCMD" != "diag" ]; then
  "$TETHER" -n -bind="" -wire="$WIRE" -just-inject="$CMD"
  exit "$matrix_rc"
fi

# Capture `fujinet diag` output while printing it, then evaluate the JamesBrown compatibility verdict.
diag_rc=0
"$TETHER" -n -bind="" -wire="$WIRE" -just-inject="$CMD" >"$DIAG_TXT" 2>&1 || diag_rc=$?
# Print everything starting from the first '=== FujiNet' header (or all output if inject failed).
python3 - "$DIAG_TXT" "$MATRIX_JSON" "$diag_rc" << 'PY'
import json, os, re, sys

diag_path, matrix_path, diag_rc = sys.argv[1], sys.argv[2], int(sys.argv[3])
raw = open(diag_path, errors="replace").read()
idx = raw.find("=== FujiNet")
body = raw[idx:] if idx >= 0 else raw
sys.stdout.write(body)
if body and not body.endswith("\n"):
    sys.stdout.write("\n")

matrix = {"ran": False, "failures": [], "hat_variant": "", "mac": ""}
if os.path.isfile(matrix_path):
    matrix = json.load(open(matrix_path))

problems = list(matrix.get("failures", []))

if diag_rc != 0 or idx < 0:
    problems.append("tether failed to run 'fujinet diag' on the host board")
else:
    pin_states = {}
    for m in re.finditer(r"GPIO\s+(\d+)\s+\([^)]+\):\s+([A-Z]+)", body):
        pin_states[int(m.group(1))] = m.group(2)

    if not matrix.get("ran"):
        if pin_states.get(8) not in (None, "FLOAT"):
            problems.append(
                f"GPIO 8 is {pin_states[8]} (expected FLOAT; unbodged 40c R2 conflicts with jamesbrown GPIO 8 PWM audio)"
            )
        if pin_states.get(9) not in (None, "FLOAT"):
            problems.append(
                f"GPIO 9 is {pin_states[9]} (expected FLOAT; unbodged 40c R1 still installed)"
            )
        if pin_states.get(10) != "HIGH":
            problems.append(
                f"GPIO 10 (J1-5 <- R5 <- ESP32 IO22 PIN_CD) is {pin_states.get(10, '?')} (expected HIGH; hat unpowered or unseated?)"
            )
        if pin_states.get(12) != "HIGH":
            problems.append(
                f"GPIO 12 (J1-9 -> ESP32 IO33 UART1_RX) is {pin_states.get(12, '?')} (expected HIGH pull-up; check J1-9->J2-8 solder joint)"
            )
        if pin_states.get(13) != "HIGH":
            problems.append(
                f"GPIO 13 (J1-11 <- ESP32 IO21 UART1_TX) is {pin_states.get(13, '?')} (expected idle HIGH; check IO13<->IO21 bodge on 41d, J1-11->J3-15 bodge on 40c, or ESP32 firmware)"
            )

    matches = re.findall(r"MATCH:\s+TX[=]GPIO(\d+)\s+RX[=]GPIO(\d+)\s+baud[=](\d+)\s+polarity[=](\S+)", body)
    jb_match = ("12", "13", "921600", "normal")
    if jb_match not in matches:
        if matches:
            tx, rx, baud, pol = matches[0]
            problems.append(
                f"FujiNet answered on TX=GPIO{tx} RX=GPIO{rx} baud={baud} polarity={pol}, "
                "but jamesbrown requires TX=GPIO12 RX=GPIO13 baud=921600 polarity=normal (run 'make flash-fujinet')"
            )
        else:
            problems.append(
                "No DriveWire OP_TIME reply on TX=GPIO12 RX=GPIO13 @ 921600 normal "
                "(flash ESP32 with 'make flash-fujinet' or check GPIO 12/13 wiring)"
            )

    ver_m = re.search(r"^\s*Version:\s*(\S+)", body, re.M)
    mac_m = re.search(r"^\s*MAC:\s*([0-9A-Fa-f:]{17})", body, re.M)
    has_hosts = "Host slots:" in body
    if jb_match in matches:
        if not ver_m or not mac_m:
            problems.append("OP_FUJI GetAdapterConfig failed (check FujiNet firmware version / OP_DWINIT framing)")
        elif not has_hosts:
            problems.append("OP_FUJI ReadHostSlots failed (LittleFS storage missing? Run 'make flash-fujinet')")

rule = "=" * 72
print(rule)
if not problems:
    ver = ver_m.group(1) if ver_m else "unknown"
    mac = mac_m.group(1) if mac_m else (matrix.get("mac") or "unknown")
    hw = matrix.get("hat_variant") or "GPIO 12/13 @ 921600 normal (plug in ESP32 USB too to test all 12 hat pins)"
    print("JAMESBROWN VERDICT: PASS — This FujiNet hat IS expected to work with jamesbrown!")
    print(f"  - Hardware:     {hw}")
    print(f"  - FujiNet FW:   {ver}  (MAC {mac})")
    print("  - Audio/GPIO 8: clear (GPIO 8/9 floating, no SN76489 PWM conflict)")
    print("  - UART0 link:   TX=GPIO12 (IO33) / RX=GPIO13 (IO21), 921600 baud, normal")
    print(rule)
    sys.exit(0)
else:
    print("JAMESBROWN VERDICT: FAIL — This FujiNet hat is NOT expected to work with jamesbrown!")
    print(f"Problems discovered ({len(problems)}):")
    for i, p in enumerate(problems, 1):
        print(f"  {i}. {p}")
    print(rule)
    sys.exit(1)
PY
