# tfr9 v5

One tree for: the **TFR/911H** single-board computer (RP2350B + 6809E/6309E), the **Copico Centipede 32Z**
CoCo cartridge, **TurboLab**, **FujiNet**, and the guest systems (TurboOS, NitrOS-9, Fuzix, Hatvan OS,
Radio Shack BASIC).  Spec and decisions: `doc/v5-notes-and-recommendations-two-and-a-half.md`.

Status: **Phase 0 (scaffold)**.  Nothing builds yet except `make doctor`.

    make help       # list targets
    make doctor     # check the toolchain (SHELF defaults to ../.. = the coco-shelf)

Build output goes to `build/`.  See `AGENTS.md` for the rules of this tree.

## Layout (see spec section 5.2)

| Dir | Purpose |
|---|---|
| `boards/` | Pico SDK board headers, pin and flash layouts |
| `third_party/` | vendored littlefs, miniz, tcl6.7c (with `PATCHES.md`) |
| `firmware/lib/` | shared RP2350B code: `core fs tcl bus mem devices images` |
| `firmware/engines/` | OS personalities (`flat65280`, `deep65280`, `gep9`, ...), parameterized by port profile |
| `firmware/products/` | thin `main.cpp` + CMake per UF2: `tfr911h`, `centipede32z`, `turbolab` |
| `host/` | the Go `tether` (one module) |
| `os/` | guest software: NitrOS-9, FujiNet kit, Hatvan, Fuzix, ... |
| `rc/` | boot-mode / `.pak`-style boot scripts |
| `tests/` | `host/` (no board) and `systests/` (board attached) |
| `scripts/`, `cmake/` | tooling |
