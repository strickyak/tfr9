# tfr9 v5

One tree for: the **TFR/911H** single-board computer (RP2350B + 6809E/6309E), the **Copico Centipede 32Z**
CoCo cartridge, **TurboLab**, **FujiNet**, and the guest systems (TurboOS, NitrOS-9, Fuzix, Hatvan OS,
Radio Shack BASIC).  Spec and decisions: `doc/v5-notes-and-recommendations-two-and-a-half.md`.

Status: **Phases 0–6 implemented and hardware-verified** (`tfr911h.uf2`, `centipede32z.uf2`, unified `tether`, 5 `e1` enhancements, TurboLab, 10 hardware `systest` suites).

    make help          # list targets
    make doctor        # check the toolchain (SHELF defaults to ../.. = the coco-shelf)
    make tether tfr911h centipede32z
    make jamesbrown-dsk jamesbrown-32z-dsk
    make release       # stage build/release/ and build/tfr911h-release.zip (8 tethers + UF2 + guest images)
    make flash-fujinet # flash FujiNet ESP32 hat firmware + LittleFS over USB
    make check-fujinet # diagnose FujiNet hat wiring/bodges, 921600-baud link, and jamesbrown verdict
    make test-host     # C++ + Go host tests (no board required)
    make systest       # 10 live hardware test suites (TFR/911H + Centipede 32z)

Build output goes to `build/`.  See `AGENTS.md` for the rules of this tree.

## Quick Demo & Helper Scripts

- **Tether REPL:** `./tether.sh` (either board), `./tether-tfr911h.sh`, `./tether-centipede32z.sh`
- **Install / Upgrade:** `./cold-install-tfr911h.sh`, `./upgrade-tfr911h.sh`, `./cold-install-centipede32z.sh`, `./upgrade-centipede32z.sh`
- **TFR/911H:** `./run-tfr911h-turbo9os.sh`, `./run-tfr911h-flat65280-nitros9-l1.sh`, `./run-tfr911h-deep65280-nitros9-l2.sh`, `./run-tfr911h-jamesbrown.sh`, `./run-tfr911h-fuzix.sh`, `./run-tfr911h-gep9-hatvan.sh`, `./run-tfr911h-coco2-basic.sh`
- **TurboLab (on TFR/911H):** `./run-tfr911h-turbolab.sh WITH trigger = r/0xFFE8 WITH trace_count = 10000`
- **Centipede 32z (on CoCo 2/3):** `./run-centipede32z-coco2-basic.sh`, `./run-centipede32z-coco3-basic.sh`, `./run-centipede32z-coco2-nitros9-l1.sh`, `./run-centipede32z-flat65280-nitros9-l1.sh`, `./run-centipede32z-deep65280-nitros9-l2.sh`, `./run-centipede32z-nitros9-coco3.sh`, `./run-centipede32z-jamesbrown.sh` (pass `-coco3` on a CoCo 3)
- **Simulated GEP9 (no board):** `./run-gep9-jamesbrown.sh`

## Layout (see spec section 5.2)

| Dir | Purpose |
|---|---|
| `boards/` | Pico SDK board headers, pin and flash layouts (`tfr911h_rp2350b.h`, `centipede32z.h`) |
| `third_party/` | vendored littlefs, miniz, tcl6.7c (with `PATCHES.md`) |
| `firmware/lib/` | shared RP2350B code: `core fs tcl bus mem devices` |
| `firmware/engines/` | `ports.tsv` + 6 `IN_RAM` Hamster engines (incl. `coco2_tfr911h.h`, `turbolab.h`) + 4 `IN_RAM` Gerbil engines |
| `firmware/products/` | UF2 products: `tfr911h` (incl. TurboLab) and `centipede32z` |
| `host/` | the Go `tether` (one module) |
| `os/` | guest software: NitrOS-9, FujiNet kit, Hatvan, Fuzix, ... |
| `rc/` | boot-mode / `.pak`-style boot scripts |
| `tests/` | `host/` (no board) and `systests/` (board attached) |
| `scripts/`, `cmake/` | tooling |
