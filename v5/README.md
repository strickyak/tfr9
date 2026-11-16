# tfr9 v5

One tree for: the **TFR/911H** single-board computer (RP2350B + 6809E/6309E), the **Copico Centipede 32Z**
CoCo cartridge, **TurboLab**, **FujiNet**, and the guest systems (TurboOS, NitrOS-9, Fuzix, Hatvan OS,
Radio Shack BASIC).  Spec and decisions: `doc/v5-notes-and-recommendations-two-and-a-half.md`.

Status: **Phases 0–6 implemented and hardware-verified** (`tfr911h.uf2`, `centipede32z.uf2`, unified `tether`, 5 `e1` enhancements, TurboLab, 10 hardware `systest` suites).

    make help          # list targets
    make doctor        # check the toolchain (SHELF defaults to ../.. = the coco-shelf)
    make tether tfr911h centipede32z
    make test-host     # C++ + Go host tests (no board required)
    make systest       # 10 live hardware test suites (TFR/911H + Centipede 32z)

Build output goes to `build/`.  See `AGENTS.md` for the rules of this tree.

## Quick Demo Scripts

- **TFR/911H:** `./run_tfr911h_turbo9os.sh`, `./run_tfr911h_nitros9_l1.sh`, `./run_tfr911h_nitros9_l2.sh`, `./run_tfr911h_fuzix.sh`, `./run_tfr911h_gep9.sh`, `./run_tfr911h_gep9big.sh`
- **TurboLab (on TFR/911H):** `./run_tfr911h_turbolab.sh WITH trigger = r/0xFFE8 WITH trace_count = 10000`
- **Centipede 32z (on CoCo 2/3):** `./run_centipede32z_coco2_basic.sh`, `./run_centipede32z_coco3_basic.sh`

## Layout (see spec section 5.2)

| Dir | Purpose |
|---|---|
| `boards/` | Pico SDK board headers, pin and flash layouts (`tfr911h_rp2350b.h`, `centipede32z.h`) |
| `third_party/` | vendored littlefs, miniz, tcl6.7c (with `PATCHES.md`) |
| `firmware/lib/` | shared RP2350B code: `core fs tcl bus mem devices` |
| `firmware/engines/` | `ports.tsv` + 5 `IN_RAM` Hamster engines (incl. `turbolab.h`) + 4 `IN_RAM` Gerbil engines |
| `firmware/products/` | UF2 products: `tfr911h` (incl. TurboLab) and `centipede32z` |
| `host/` | the Go `tether` (one module) |
| `os/` | guest software: NitrOS-9, FujiNet kit, Hatvan, Fuzix, ... |
| `rc/` | boot-mode / `.pak`-style boot scripts |
| `tests/` | `host/` (no board) and `systests/` (board attached) |
| `scripts/`, `cmake/` | tooling |
