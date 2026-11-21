# v5 status and hand-off — 2026-11-21

For **Strick** and for **the next agent**.  Read this first, then `doc/v5-nov-14.md` (earlier Phase 0–6 history) and
the accepted spec (`doc/v5-notes-and-recommendations-two-and-a-half.md`, especially §§7, 11, 13, 15, 16, and Appendix F/G).
State at time of writing: branch **`work`**, HEAD `54e017a` + this doc, working tree clean (aside from local untracked
notes `TODO`, `TODOC`, `H.tnfsfuse` which are intentionally not committed).

---

## 0. One-paragraph summary

Both firmware products (`build/tfr911h.uf2` and `build/centipede32z.uf2`), the cross-platform Go `tether` (with
built-in `foggynet` simulated FujiNet, `--session` / `:8080..:8099` auto-stepping / `-just-list-sessions`, default
`TIOCEXCL` serial port locking, and live CoCo 2 VDG + CoCo 3 GIME WebSocket screen streaming), the Go TNFS tools
(`host/cmd/tnfsd` and `host/cmd/tnfsfuse`), the **6 `IN_RAM` Hamster foregrounds** (`hamster_flat`,
`hamster_turbolab`, `hamster_gime`, `hamster_gep9`, `hamster_multicomp`, `hamster_coco2`) and **4 `IN_RAM` Gerbil
foregrounds** (`gerbil_io`, `gerbil_sam64k`, `gerbil_gime`, `gerbil_gep9`) with **0 core-1 SRAM→flash calls**
(`make check-core1`), hyphenated `run-*.sh` / `cold-install-*.sh` / `upgrade-*.sh` / `tether-*.sh` scripts, both
FujiNet NitrOS-9 Level 2 disks (`build/tfr911-jamesbrown.dsk` and `build/centipede32z-jamesbrown.dsk` with `basic09`,
all 5 CMOC C apps, and WiFi scan/connect fixes), CoCo 2 & CoCo 3 BASIC demo disks (OPIL + `HELLO`, `PMODE1/2/4`,
`HSCREEN2/4`), hardware diagnostics (`make check-fujinet` and `make check-rp2350b`), and `build/tfr911h-release.zip`
(`make release`) are built and verified on live hardware (populated TFR/911H `yksi` and Centipede 32z `z10` in a 16 KB
CoCo 2 with a 40C-bodged FujiNet hat).  **Section 4 below is the complete checklist of deferred, remaining, and
spec-parked work.**

---

## 1. Ground rules reminder (`AGENTS.md`)

1. **Never edit, build in, or add files to `../v4/`** (or `~/github.com/dillera/tfr9`).  v5 must not depend on `../v4/`
   at build or test time.  All build intermediates and outputs go in `v5/build/`.
2. **Commit only on `work*` branches** (currently `work`).  Do not modify `main`.  Commit with **explicit file paths**
   (never `git add -A`).
3. **Import from v4 only via `scripts/import-from-v4.sh <path>...`** (`git archive`), never `cp -r`.
4. **Never read or modify files with mode `000`.**
5. **Shell scripts must be portable POSIX `sh`** (`make check-sh`).  Top-level `*.sh` scripts use hyphens (`-`), not
   underscores (`_`).  Note: `scripts/check-sh.sh` greps for `=(` to catch bash arrays, so write `[=](` inside embedded
   Python/awk regexes in `.sh` files.
6. **Never print or commit WiFi passwords or private credentials.**

---

## 2. Live hardware & multi-board workflow

Both boards can remain plugged into the same host at the same time; all top-level scripts auto-select the matching
board by flash label (`scripts/find-board.sh`) and lock its serial device exclusively (`TIOCEXCL`):

* **TFR/911H SBC (`yksi`)**: `b=tfr9,v=911h,id=yksi` (RP2350B + Hitachi HD63C09E; optional 41d-bodged FujiNet hat).
* **Centipede 32z (`z10`)**: `b=centipede,v=32z,id=z10` (RP2350B in a 16 KB CoCo 2; optional 40c-bodged FujiNet hat).

Common commands:

```sh
make doctor && make check-sh && make test-host      # toolchain, POSIX sh lint, C++ + Go host tests
make tether tfr911h centipede32z                    # native tether + both UF2s (runs check-core1 & check-flash-size)
scripts/find-board.sh                               # list attached RP2350B boards by label
./upgrade-tfr911h.sh                                # reflash TFR/911H (or ./cold-install-tfr911h.sh)
./upgrade-centipede32z.sh                           # reflash Centipede 32z (or ./cold-install-centipede32z.sh)
make check-rp2350b                                  # 48-GPIO impulse/clock/short/bus probe (WIRE=... for 2+ boards)
make check-fujinet                                  # FujiNet hat GPIO + 921600-baud probe (--centipede on 32z)
build/tether -just-list-sessions                    # list active tether HTTP/WebSocket sessions on :8080..:8099
```

---

## 3. What was completed since `doc/v5-nov-14.md` (Nov 15 – Nov 21)

### 3.1 TFR/911H, Gep9 unification, and terminal UX
* **Always-embiggened `Gep9MMU`** (`4095f39`): unified `gep9` and `gep9big` (aligned with `hatvan-os` and `minigolf`
  dropping `--embiggen`), saving ~17 KB of firmware BSS; launcher is `./run-tfr911h-gep9-hatvan.sh`.
* **Tether terminal modes & `^]`/`^C`** (`dde3a02`, `587aae6`, `a2f842d`): added `-c` (`-cooked`), `-r` (`-readline`),
  `-n` (`-nostdin`), and default cbreak mode with `-pass_ctrl_c` (`Ctrl-C` forwarded to guest, `Ctrl-]` exits tether);
  documented in `doc/tether-terminal-modes.md`.
* **Emulated CoCo 2 Disk BASIC on TFR/911H** (`733678e`): added `hamster_coco2` (`on tfr911h run rsbasic for coco2`)
  and `./run-tfr911h-coco2-basic.sh` with live browser VDG screen at `:8080`.
* **TFR/911H binary release bundle** (`33183d1`): renamed `tether` `-quick-*` flags to `-just-*`, added Phase-1
  `-just-set-label` PicoRPC, cross-built 8 `tether.<os>-<arch>.exe` binaries (`make tethers`), and created
  `make release` -> `build/tfr911h-release.zip` (`scripts/make-release.sh`, `doc/release-tfr911h-README.md`).

### 3.2 Centipede 32z, CoCo 2 / CoCo 3 engines, and BASIC / NitrOS-9 launchers
* **Floppy geometry & `HALTENA` fixes** (`a62e3af`, `7be1143`): `firmware/products/centipede32z/floppy.h` now
  auto-detects single/double density, single/double sided, and 35/40/80 tracks from disk image size, and resolves the
  `$FF40` `HALTENA` write race.
* **Centipede 32z NitrOS-9 L1/L2 suite** (`39c7bae`, `e4f5612`, `7be1143`): verified `./run-centipede32z-coco2-nitros9-l1.sh`,
  `./run-centipede32z-flat65280-nitros9-l1.sh`, `./run-centipede32z-deep65280-nitros9-l2.sh`, and
  `./run-centipede32z-nitros9-coco3.sh` on the 16 KB CoCo 2.
* **Live CoCo 3 GIME/MMU WebSocket screen & BASIC demo disks** (`d25f73a`, `a49dbb9`, `9020b52`, `de8e039`):
  fixed physical block `$3C..$3F` `TY=0` ROM decode in `firmware/engines/gerbil_guts.h`, streamed GIME/MMU/Palette/VRAM
  writes to `host/cmd/tether/samvdg.go` (including 640->320 4-pixel/byte downsampling), and built
  `build/coco2_basic.dsk` (OPIL + `HELLO`, `PMODE1`, `PMODE2`, `PMODE4`) and `build/coco3_basic.dsk` (+ `HSCREEN2`,
  `HSCREEN4`).

### 3.3 FujiNet, `jamesbrown` / `jamesbrown-32z`, `foggynet`, and Go TNFS tools
* **FujiNet NitrOS-9 L2 disks** (`8749129`, `2e77de7`, `bbf2936`, `7826319`): `make jamesbrown-dsk`
  (`build/tfr911-jamesbrown.dsk`) and `make jamesbrown-32z-dsk` (`build/centipede32z-jamesbrown.dsk`, 6809E CoCo-safe
  `$FF41` Becker / `$FF88` ACIA / `e1` ports) both bundle `CMDS/basic09` and all 5 CMOC C FujiNet apps (`fnnews`,
  `fnweather`, `fnstocks`, `fnbounce`, `fnbattleship` via `scripts/build-jamesbrown-fn-apps.sh`).
* **Centipede 32z FujiNet WiFi scan & DHCP fixes** (`7826319`): patched `scdwv.asm` `SS.Fuji` with a short (~80 ms)
  probe for `FUJI$Ready` (`$E2 $00`) so no-hat boots fail fast while giving active FujiNet commands a ~16–31 s initial
  timeout for ~4.5 s 2.4 GHz scans; patched `fnconfig.as` to retry across upstream ESP32 `ESP_ERR_WIFI_STATE` (`12294`)
  reconnect bursts and wait for DHCP before printing IP status.
* **Host `foggynet`, `tnfsd`, and `tnfsfuse`** (`1392a9d`, `4785f49`, `af582eb`): added simulated FujiNet (`foggynet`,
  TCP-first TNFS) in `tether` (`./run-gep9-jamesbrown.sh`, `doc/jamesbrown-fujinet-guide.md`), read-only TCP TNFS
  server `host/cmd/tnfsd` (with path-traversal confinement), and read-only TCP TNFS FUSE client `host/cmd/tnfsfuse`.

### 3.4 Multi-session locking, provisioning scripts, and hardware diagnostics
* **Exclusive locking & named sessions** (`9f1a2c8`..`fd5575b`): `tether` acquires `ioctl(TIOCEXCL)` by default
  (`--nonexclusive` override, fast fail on `EBUSY`), auto-steps HTTP ports `:8080..:8099`, serves
  `<port> <session> <b> <v> <id> <name>` at `/session` and `tether -just-list-sessions` (`scripts/find-session.sh`),
  validates `[!-~]+` tokens, and sets `<token> Console` as the browser `<title>`.
* **Provisioning & launcher consistency** (`a7fced7`, `fe37779`, `288843e`): added `cold-install-centipede32z.sh`,
  `upgrade-centipede32z.sh`, `tether-tfr911h.sh`, `tether-centipede32z.sh`, and hyphenated all top-level `*.sh` names.
* **FujiNet & RP2350B GPIO diagnostics** (`795edd8`, `ec21044`, `54e017a`):
  * `make flash-fujinet` (`scripts/flash-fujinet.sh`) and `make check-fujinet` (`scripts/diagnose_fujinet.sh`,
    `firmware/lib/devices/fujinet_diag.h`, `--centipede` flag for 32z).
  * `make check-rp2350b` (`scripts/diagnose_rp2350b.sh`, `firmware/lib/devices/rp2350b_diag.h`): `rp2350b` Tcl command
    with `IN_RAM` leaf probes (`SampleBurst48Sram`, `CountEdgesSram`, `ShortOutputImpulseSram`) testing all 48 GPIOs
    for clocks (`E`/`Q` quadrature on Centipede), external drivers vs `HI_Z` vs weak pulls (~12 ns 1-cycle impulse),
    adjacent/all-pairs `HI_Z` shorts, FujiNet hat signatures, and TFR/911H 63C09E reset-vector bus health.

---

## 4. Deferred & remaining work (handoff checklist)

### 4.1 Tactical items deferred recently
1. **CoCo 3 `HPRINT` in `HSCREEN` & `HSCREEN` drawing speed optimization**
   * *Context:* While adding `HSCREEN2.BAS` and `HSCREEN4.BAS` to `build/coco3_basic.dsk` (`de8e039`), Super Extended
     Color BASIC's `HPRINT` misbehaved under `gerbil_gime` ("Become CoCo 3" on a 16 KB CoCo 2), so we shipped pure
     `HLINE` star patterns and deferred `HPRINT` investigation + drawing speed optimization.
   * *Where to look:* `scripts/make-coco3-basic-dsk.sh`, `firmware/engines/gerbil_guts.h` (`GerbilGime` `$FF90..$FFBF`
     register reads, `$FE00..$FEFF` `MC3` constant-RAM page, and Super Extended BASIC ROM font routines), and
     `host/cmd/tether/samvdg.go`.
2. **Upstream `fujinet-firmware` `fnWiFi::scan_networks()` reconnect-state bug (and TFR/911H `fnconfig` symmetry)**
   * *Context:* In upstream `fujinet-firmware` (`lib/hardware/fnWiFi.cpp`), `fnWiFi::scan_networks()` only calls
     `esp_wifi_disconnect()` when `_connected == true`.  While `WiFiManager` is mid-reconnect to an unreachable saved
     SSID, `_connected` is `false` while the ESP-IDF STA state machine is busy, so `esp_wifi_scan_start()` immediately
     returns `12294` (`ESP_ERR_WIFI_STATE`) and `0` APs.
   * *Where to look:* Already worked around in `scripts/make-jamesbrown-32z-dsk.sh` (`fnconfig.as` retry + `scdwv.asm`
     timeout split).  Optionally port the same `scdwv.asm` / `fnconfig.as` patch into `scripts/make-jamesbrown-dsk.sh`
     (for TFR/911H `jamesbrown` parity) and/or call `esp_wifi_disconnect()` unconditionally before `esp_wifi_scan_start()`
     in the local `fujinet-firmware` checkout used by `scripts/flash-fujinet.sh`.

### 4.2 Remaining hardware & OS matrix coverage
1. **Fuzix on Centipede 32z (`on coco2 run fuzix for coco2`)**
   * *Context:* Declared in `firmware/engines/ports.tsv` (`gerbil_gime`, console `$FF60..$FF61`, byte-FIFO SD
     `$FF68..$FF69`) and wired in `firmware/products/centipede32z/centipede_targets.h`, but has no
     `run-centipede32z-fuzix.sh` launcher or live CoCo 2 hardware test yet.
   * *Next steps:* Locate/stage the `coco2centipede` Fuzix kernel + root disk image, add `run-centipede32z-fuzix.sh`,
     and verify boot + userland on `z10`.
2. **Hatvan on Centipede 32z (`on coco2 run hatvan for coco2`)**
   * *Context:* Declared in `firmware/engines/ports.tsv` (`gerbil_gep9`: `disk` `$FF50..$FF5B`, `console`
     `$FF60..$FF65`, `fuse_dma` `$FF80..$FF8F`, `embiggen_mmu` `$FFE0..$FFE7`, `task_sec` `$FFE8..$FFEE`; Spec R4).
   * *Two pieces needed:*
     1. Build `hatvan-os` and `minigolf` userland for the `coco2` port map (separate from `gep9`'s `$FF00..$FF47`).
     2. Verify `gerbil_gep9` on a real 0.895 MHz CoCo 2 where the SAM drives `E`/`Q` continuously (unlike TFR/911H,
        `gerbil.pio` cannot stretch `E`/`Q` during synchronous Gep9 disk VFS RPCs — `gerbil_gep9` will likely need to
        assert `HALT#` across disk RPCs, similar to `floppy.h`).
3. **Physical CoCo 3 hardware verification (`on coco3 ...` / `gerbil_io`)**
   * *Context:* All 7 `on coco3 ...` targets in `firmware/engines/ports.tsv` (`gerbil_io`, using a real CoCo 3
     motherboard's GIME, MMU, and RAM) compile cleanly and pass port-conflict host tests, while CoCo 3 software has so
     far been tested on the 16 KB CoCo 2 via `gerbil_gime` (`on coco2 run ... for coco3`).
   * *Next steps:* Plug a Centipede 32z into an actual CoCo 3 and verify `on coco3 run rsbasic for coco3`,
     `on coco3 run nitros9l2 for coco3`, and `on coco3 run nitros9l2 for jamesbrown` (plus add `--coco3` or dedicated
     `run-centipede32z-coco3-*.sh` scripts as appropriate).
4. **Unpopulated-board & CoCo-off runs of `diagnose_rp2350b.sh`**
   * *Context:* `make check-rp2350b` (`scripts/diagnose_rp2350b.sh`) has been verified twice back-to-back on populated
     TFR/911H (`yksi`) and populated Centipede 32z (`z10` in powered CoCo 2 + FujiNet hat).
   * *Next steps:* Run `make check-rp2350b` on fresh-out-of-the-box unpopulated TFR/911H and Centipede 32z boards
     (expecting 45/45 probeable GPIOs `HI_Z`, `0` clocks, `0` shorts, board type inferred via flash label or `--board`)
     and on a Centipede 32z plugged into a powered-off CoCo.

### 4.3 Release packaging & pre-release polish
1. **Centipede 32z release zip & `make release` updates**
   * *Context:* `scripts/make-release.sh` currently produces `build/tfr911h-release.zip` and predates both
     `scripts/diagnose_rp2350b.sh` and the Centipede 32z cold-install/upgrade/launcher scripts.
   * *Next steps:*
     * Include `scripts/diagnose_rp2350b.sh` in `scripts/make-release.sh` and `doc/release-tfr911h-README.md`.
     * Write `doc/release-centipede32z-README.md` and add a `release-centipede32z` (or unified dual-board `release`)
       target packaging `build/centipede32z.uf2`, the 8 `tether.*.exe` binaries, `cold-install-centipede32z.sh`,
       `upgrade-centipede32z.sh`, `tether-centipede32z.sh`, all `run-centipede32z-*.sh` scripts, and the CoCo 2/3 BASIC,
       NitrOS-9 L1/L2, and `centipede32z-jamesbrown.dsk` disk images.
2. **Populate `/rc` standalone boot scripts (`rc/`)**
   * *Context:* `rc/README.md` is still a 2-line placeholder.  Define and populate the canonical `/rc/init.tcl` and
     `/rc/modeN.tcl` catalog for standalone LittleFS boot modes.
3. **Revisit `TCL_MEM_DEBUG=1` before final v5.0 release (Spec R8)**
   * *Context:* Kept enabled during development per Spec Q10/R8; decide before tagging a release whether to disable it
     in production UF2 builds to reclaim SRAM and `ckalloc`/`ckfree` overhead.
4. **Minor spec polish items**
   * *Generated guest port headers (Spec §7 / R10):* `scripts/gen-ports.sh` currently generates
     `build/generated/ports_gen.h` and `build/generated/io-map.md` from `firmware/engines/ports.tsv`; optionally emit
     NitrOS-9 `.d` assembly definitions / C headers / Go `gep9` tables if needed.
   * *Linux `openat2` fast path (Spec §11.13f):* `host/internal/pcfs/pcfs.go` enforces symlink-free `/pc` confinement
     portably with per-component `Lstat` + `O_NOFOLLOW` + `os.SameFile`; adding Linux
     `openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS)` remains an optional hardening step.

### 4.4 Explicitly parked in the v5 spec (§15–§16)
1. **`.pak` software pack format & launcher (Spec Appendix F / R16)**
   * Streaming zip VFS (`/dir/file.zip!/...`) is implemented and tested (`tests/systests/05_zip_stream_test.sh`);
     defining the `.pak` bundle layout (`zip -0` for large images) and `/rc` launcher convention is parked for later.
2. **Tether-less FujiNet-root boot (`l2fn`, Spec R15)**
   * Booting NitrOS-9 Level 2 `/DD` directly from FujiNet (`boot_dw` / `/X0`) without `/pc` or `/lfs` `EmuDsk`.
3. **Portable fast console output path in `term.c` (Spec Q14 / R1)**
   * Kept direct `sta` to `ACIA_DATA` (`$FF89` on CoCo-safe `jamesbrown-32z`, `$FF15` on TFR/911H `jamesbrown`) so
     CMOC apps like `fnbounce` maintain ~9.3 fps instead of ~2.8 fps with byte-at-a-time `I$WritLn`; designing a
     portable bulk-write driver / `SetStat` fast path is parked.

---

## 5. Hardware & firmware lessons added since Nov 14

* **Centipede 32z: clear `IO_COCO2` before pausing `pio0` SM0.**  If `pio0` SM0 (`gerbil.pio`) is disabled while
  `tcl_io::IO_COCO2` remains set in `tcl_io::active_io`, `gspoon.h`'s post-command `redraw_line()` blocks forever
  waiting for Core 1 to drain `BG2FG_POKE`.  Always clear `tcl_io::active_io &= ~tcl_io::IO_COCO2` first (if a board
  ever wedges in USB CDC, opening its serial port at `1200` baud triggers Pico SDK BOOTSEL recovery without touching the
  hardware).
* **RP2350B GPIO pad + 2-FF input synchronizer latency.**  At 250 MHz (`4 ns`/cycle), reading `sio_hw->gpio_in`
  immediately after `sio_hw->gpio_oe_clr` returns the *pre-release* driven level still in the pad + 2-FF input
  synchronizer pipeline.  Wait ~6 `nop`s (~24 ns) before sampling `s_fast`.
* **TFR/911H `_RESET` RC rise time & CMOS HD63C09E reset state.**  `_RESET` (GPIO 26) is open-drain with only RP2350B's
  ~50 kΩ internal pull-up (~5 ms RC rise time), and a Hitachi HD63C09E outputs uninitialized latch values while
  `RESET#=0` (unlike NMOS 6809E's `$FFFF`).  To observe clean `$FFFE -> $FFFF` vector fetches, hold `RESET#=0, HALT#=0`
  while clocking ~1 MHz `E`/`Q`, release `RESET#` while keeping `HALT#=0` for ~6 ms, then release `HALT#`.
* **6809 `leau` vs `leax` in poll loops.**  On 6809/6309, `leax` and `leay` update `CC.Z`, whereas `leau` and `leas` do
  **not** affect any condition codes — never end a countdown loop with `leau -1,u` + `bne`.
* **CMOC parallel build temp-file collisions.**  Upstream FujiNet CMOC `Makefile`s compile multiple `.c` files in one
  directory using shared intermediate names; build them sequentially (`scripts/build-jamesbrown-fn-apps.sh`), never
  with `make -j`.
