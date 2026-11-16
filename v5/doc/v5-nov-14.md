# v5 status and hand-off — 2026-11-14

For **Strick** and for **the next agent**.  Read this first, then the spec
(`doc/v5-notes-and-recommendations-two-and-a-half.md`, "the 2.5 doc"; Appendix G is the implementation log).
State: branch **`work`**, HEAD `3d7f886`, working tree clean (except files that are deliberately never committed,
see §1).  Time of writing: 2026-11-14 ≈ 23:45.

---

## 0. One-paragraph summary

v5 is a copy-then-modify of v4 (`../v4/`, never touched).  Both firmware products (`tfr911h.uf2` and `centipede32z.uf2`), a unified Go `tether` (including full TurboLab cycle-accurate trace decoding, `.list` relocation, OS-9 `SWI2`/`RTI` API decoding, and CoCo 2/3 VDG/GIME screen capture), the test harness, the `on <tfr911h|coco2|coco3> run <os> ?for <target>? ?with ...?` (repeated `with`) device/port architecture, the **5 `IN_RAM` Hamster foregrounds** (`hamster_flat`, `hamster_turbolab`, `hamster_gime`, `hamster_gep9`, `hamster_multicomp`) and **4 `IN_RAM` Gerbil foregrounds** (`gerbil_io`, `gerbil_sam64k`, `gerbil_gime`, `gerbil_gep9`), **0 core-1 SRAM→flash calls** (`make check-core1` strict on both products), **all 5 `e1` enhancement devices** (`BeckerPort`, `AudioSn76489`, `RtcPort`, `Orchestra90`, `InfoPort`), **all 23 `(on, run, for)` targets** in `ports.tsv`, shell-free `quick-reflash`, and portable POSIX `sh` demo scripts (`run_*.sh`) are in place and **verified on real hardware**: **ten** system-test suites pass across both live boards (TFR/911H and Centipede 32z + 16 KB CoCo 2), including all 6 guest OS configurations on TFR/911H (`turbo9os`, `flat65280`, `deep65280`, `hatvan/gep9`, `hatvan/gep9big`, `fuzix`).  **Remaining:** guest kit (`os/fujinet`, generated `.d`/`.go` port headers) and `.pak` loader (Phase 7).

---

## 1. Ground rules (from `AGENTS.md` and Strick's standing instructions)

1. **Never edit, build in, or add files to `../v4/`** (or `~/github.com/dillera/tfr9`).  Running a v4 *binary* is
   fine; building v4 is not.  v5 must not depend on v4 at build or test time.
2. **Commit only on `work*` branches** (currently `work`).  Don't touch `main`; don't push.
3. **Import from v4 only with `scripts/import-from-v4.sh`** (uses `git archive`), never `cp -r`.
4. **Files with mode 000 are not read or changed.**
5. **Shell scripts are portable POSIX `sh`** (macOS users): no `[[ ]]`, arrays, `local`, `source`, `echo -e`,
   `sort -V`, `readlink -f`, `sed -i` without care, `grep -P`, `stat -c`, `date -d`, `xargs -d`.  `make check-sh`
   enforces it.  Makefiles stay GNU-make-3.81 compatible.
6. All build products go in `v5/build/` (git-ignored).
7. Commit with **explicit paths**.  Never `git add -A` at the repo root: `tfr9/hat-fuji-41d/*.kicad_*` are
   Strick's own uncommitted edits.  Also untracked and never committed: `v5/.agy_conversation_id*`, `v5/doc/RCS/`,
   vim `.un~` files.
8. Strick's style: concise answers, GitHub markdown, decisions logged in the spec, "snags for the unwary"
   recorded, honest verification (mutation tests, differential tests, parity builds, real hardware).  If a
   decision "doesn't work well" you may change it, but **list the revision** (spec §16) for review.  Ask with
   `ask_question` when a design choice is his.  He has asked: *use hardware to verify; check back if stuck.*
9. Things that cost hours — don't repeat them (details in §9): run tether with `-n </dev/null` from scripts;
   never trust a free-memory estimate; never use lambdas as I/O handlers; don't `pkill -f` a pattern that appears in
   your own command line; don't guess `/dev/ttyACM*` numbers.

---

## 2. The hardware and how to work with it

* **TFR/911H SBC** (RP2350B + Hitachi 63C09E): label `p=1,b=tfr9,v=911h,mhz=250,h=pizga.net,id=uks,…`.  *(Note: physical UF2 flash label is `b=tfr9,v=911h`; Tcl host name is `tfr911h`.)*
* **Centipede-32z** on a CoCo2 (16 KB): label `b=centipede,v=32z,…,name=Vinelix,id=z10`.  Flashed and verified with `build/centipede32z.uf2` (`09_centipede_coco2_test.sh`, `run_centipede32z_coco2_basic.sh`, `run_centipede32z_coco3_basic.sh`).
* A CH340 USB-serial adapter (`/dev/ttyUSB0`) appeared during the session; not ours.
* `/dev/ttyACM*` numbers change.  **Find boards by label:** `scripts/find-board.sh b=tfr9 v=911h` prints the stable
  `/dev/serial/by-id/usb-Raspberry_Pi_Pico_<serial>-if00` name (Linux).  With no arguments it lists every board.
* **Flash** (picotool cannot open the USB device without extra permissions, and we do not use sudo):
  `build/tether -n -bind="" -wire=$(scripts/find-board.sh b=tfr9 v=911h) -quick-reflash=$PWD/build/tfr911h.uf2 </dev/null`
  (takes ~15 s; it asks the board to enter BOOTSEL and copies the UF2 to `/media/$USER/RP*`).
* **A hung board needs a physical RESET** (ask Strick).  A *panic/abort* prints `*** ABORT: …` and hangs by design.
  To revert firmware: `../v4/build/tfr911h_v4.uf2` (TFR) and `../v4/centipede-32z/build/centipede.uf2` (Centipede).
* Engine tests need images from outside the repo (see §4).  Fuzix paths used so far:
  `V5_IMG_FUZIX_BIN=/home/strick/nand/github.com/EtchedPixels/FUZIX/Kernel/fuzix.bin`,
  `V5_IMG_FUZIX_DISK=/home/strick/nand/github.com/EtchedPixels/FUZIX/Images/multicomp09/disk.img`.
* The board can carry **old `/rc/modeN.tcl` files from earlier firmware** that win over `/pc/modeN.tcl` (S55).

Quick start:

```sh
cd /home/strick/modoc/coco-shelf/tfr9/v5
make doctor && make test-host        # toolchain check; no-board tests
make tether && make tfr911h          # build/tether and build/tfr911h.uf2 (~40 s)
scripts/find-board.sh                # list boards
# flash (above), then:
export V5_IMG_FUZIX_BIN=… V5_IMG_FUZIX_DISK=…
make systest </dev/null > build/systest.log 2>&1; grep -E "FAIL|Total:|Suites|SUCCESS" build/systest.log
```

---

## 3. What we accomplished

### 3.1 Foundation (Phases 0–2, 2026-10-30)
* Scaffold: `AGENTS.md`, `README.md`, `Makefile` (`doctor check-sh check-io check-core1 tfr911h tether tethers
  gen-ports test-host systest clean`), `scripts/`, skeleton directories.
* **Phase 1:** the TFR/911H firmware include-closure imported verbatim from v4 (@`f963d3f`); a build of v4 HEAD and
  of v5 had identical symbols.  Zero-copy `cobs.h` (fixes the ≥1024-byte stack overflow, S26); board header with
  `PICO_RP2350A 0` and 16 MB flash (S35, S43).
* **Phase 2:** one Go module `host/` (`cmd/tether`, `internal/*`) = v4's two tether trees 3-way merged + Diller's
  patches (disk.go shadow fix, UTF-8, `-pass_ctrl_c`, `-boot`, `T_WALLTIME`).  **`/pc` confinement**: symlinks
  invisible (per-component `Lstat` + `O_NOFOLLOW` + `SameFile`), root verified on every request and never created,
  read length capped at 64 KB, default `-pc /tmp/pc`.
* `ports.tsv` + `scripts/gen-ports.sh` (profiles `TFR4`, `COCO`, enhancement map `E1`; `$FF7F` forbidden) and the
  `IoMap` claim registry with host tests.

### 3.2 Memory and robustness (2026-11-14) — all verified on the TFR/911H
* **Streaming `source`** (S38): one command at a time (`tcl_stream.h`), a 349 KB script sources in ~2 s.
* **Zip/.pak entries streamed** (S50): `VfsNode::clone_for_open()` gives each open entry its own parent handle;
  *stored* entries are read straight from the archive (seekable), *deflated* ones via miniz's iterator; miniz I/O
  buffer 4 KB.  **Pak rule: STORE big entries (`zip -0`).**
* **Out of memory is a Tcl error** (S49): `Tcl_LowMemoryHook` (small Tcl patch) refuses the next command when free
  heap < 16 KB; `centipede heap-free|heap-info|heap-probe`; `core/heap.h` has `MustAlloc` / `TryAlloc`.
* **`malloc` returns NULL** (S51): the Pico SDK wrapper used to `panic()` on failure, so *no* NULL-handling code
  ever ran.  `PICO_MALLOC_PANIC=0`; `operator new` / `abort()` stop the board **with a visible message**
  (`core/oom_abort.h`).  `HeapCanHold` was removed: **allocate and check for NULL, or use a fixed policy limit.**
  Size limits: `multifile` 16 KB, `edit` 12 KB; `zip get` uses `TryAlloc`.
* S52: under `TCL_MEM_DEBUG`, `Tcl_SetResult(…, free)` means "ckfree this" — hand Tcl a malloc buffer with a
  *distinct* free proc (`tcl_free_malloced`).

### 3.3 Tether, restart and the test harness
* `scripts/find-board.sh` (parallel label probes, by-id names), harness never lets tether guess a device (S48).
* tether needs **`-n` and `</dev/null`** in scripts or it waits on stdin (S47).
* **Restart reliability** (S53/S54): the firmware waits until the host opens the port (DTR) after a boot-mode
  restart instead of a fixed 3 s; tether's serial descriptor is now poller-managed so `Close()` really closes (DTR
  drops and re-rises), waits for the device node to be replaced (≥0.5 s, ≤8 s), never rewrites its wire to "the
  last `/dev/ttyACM*`".  **A restart takes 2–3 s** (it was 5–6 s, and sometimes never).

### 3.4 Phase 3 — devices and engines
1. **`main.cpp` split** (2 619 → 533 lines) by pure code motion into `engines/*.h`, `lib/devices/*.h`,
   `lib/mem/deep_mmu.h`, `products/tfr911h/*.h`.  The flat firmware binary was **byte-identical** before and after.
   `scripts/elf-parity.sh BEFORE.elf AFTER.elf` is the tool for proving any pure refactor.
2. **Engine registry** `engines/registry.h` (`EngineSpec{name, setup}`, `FindEngine`; canonical names only, all
   aliases removed on 2026-11-15).
3. **Devices as structs with `Install(base)`** (`EmuDsk`, `ClockStop`, `TermAcia`, `Turbo9SimAcia`, `Deep*`, `Gep*`,
   `FlatVectors`, …): each claims its range in an `IoMap` and stores *named `IN_RAM`* handlers.  `IoFinish()` prints
   `[io-map: <engine> matches the plan (profile P, N ranges, M settings)]` at every launch.  Old-vs-new dispatch
   tables were compared entry by entry on the board: identical (512/512) for all table-driven engines.
4. **No lambda I/O handlers any more** (they cannot be `IN_RAM`); `make check-io` enforces it, no exceptions.
5. **Target & port configuration from Tcl** (decisions in spec §15.x / §15.y): user-facing grammar
   `on <host> run <os> ?for <target>? ?with ...?` (case-insensitive, `0x...` hex barewords) compiles down into flat
   lowercase key-value RAM-only settings (`lib/devices/ports_config.h`); `ports` has two subcommands (`ports list`,
   `ports check`); legacy `Config(engine)` and `enhance` were removed.  `bye` validates the placement, host hardware,
   and launcher first — a conflict, `$FF7F`, CoCo motherboard collision, wrong host, unimplemented target, or obsolete
   `Config(engine)` makes `bye` fail with a clear message and **leaves the board at `TCL>`**.  Engines install at the
   configured addresses; the embiggen test in the bus loop uses `(addr - gep9_embiggen_base) & 0x07` (and both `gep9`
   and `gep9big` include `embiggen_mmu` in `ports.tsv`).
6. **Naming:** `(run_os, for_hw)` launchers in `engines/registry.h` (`turbo9os/turbo9sim`, `nitros9l1/flat65280`,
   `nitros9l2/deep65280`, `hatvan/gep9|gep9big|coco2`, `fuzix/multicomp09`), plus planned targets including
   `nitros9l2/jamesbrown` (Diller L2 addresses) and `nitros9l2/coco3` (CoCo-compatible L2 on `tfr9`, `coco2`, `coco3`).

### 3.5 Commits (newest first, v5-related)
`3d7f886` names/ensemble · `401796a` ports commands · `7a81bfe` spec §15.x · `c26a59b` engines converted ·
`77a4c0d` devices as structs · `b13ce44` registry · `097ca6e` main.cpp split · `77ba9e1` restart/tether ·
`7bc361e` engine tests · `5bc36ba` HeapCanHold removed · `c96723b` malloc NULL · `f2398dc` zip streaming ·
`bc80112` OOM as Tcl error · `60e8a5a`/`8d4f173`/`5b39fc0` harness · `3eca4a1` systests · `d5de52f` streaming
source · `d1f6e02` ports.tsv · `e95c7d0` tether · `9ec70b1` import · `0325add` scaffold.

---

## 4. Tests — what exists and what each proves

`make test-host` (no board): `cobs_test`, `iomap_test`, `ports_config_test`, `test_pio_assembler`, `tcl_stream_test`, `tcl_lowmem_test`
(C++, built against the real Tcl 6.7c) + `go test ./...`.  The host Makefile has **one** `test:` rule on purpose
(make keeps only the last recipe per target — this once silently disabled tests).

`make systest` (board; skips cleanly without one; ~5 min).  Each suite starts from what it needs; `common.sh` finds
the board by label (`BOARD_MATCH`, default `b=tfr9 v=911h`; `WIRE=` overrides).

| Suite | Checks | Covers |
|---|---|---|
| 01 quick RPC | 13 | label, ping, restart, upload, `--exit` |
| 02 Tcl/VFS | 19 | REPL, `/pc` file access and md5, symlink refusal, missing `/pc`, LittleFS, console redraw |
| 03 streaming source | 9 | small, error stop, nested, 349 KB script, board alive |
| 04 heap | 9 | `heap-free`, runaway loop → catchable "out of memory", memory returns, no reboot |
| 05 zip | 17 | stored 200 KB + deflated 207 KB entries (md5 equals PC), `source` from zip, `zip get` limits, `multifile` refusal, fragmentation survival |
| 06 engines (expect) | 6 | Turbo9OS, Flat65280 (L1), Deep65280 (L2), Hatvan (`gep9` & `gep9big`), Fuzix boot and run commands |
| 07 ports | 66 | `on/run/for/with` (host `tfr911h`, repeated `with`, `,` & `tfr9` rejection), `turbolab` `key = value` validation, `ports list\|check`, `gep9`/`gep9big`+Becker clash and fixes, `coco2` vs `coco3` motherboard checks, `bye` refusal keeps `TCL>` |
| 08 enhancements | 23 | Tcl `click`/`beep`/`fujinet stats`; 6309 `flat65280+e1` (SN76489, Orchestra-90, Becker, `T_WALLTIME` RTC, InfoPort); 6309 `nitros9l2 for coco3` (`.t35` launcher, 60 Hz GIME VBORD IRQ, Palette); 6309 `nitros9l2 for jamesbrown` (Track-34 disk boot + Diller I/O map) |
| 09 centipede32z + CoCo 2 | 30 | live Centipede 32z (`b=centipede v=32z`) on 16 KB CoCo 2: label/ping/heap, `on`/`run`/`ports` PIA/SAM/GIME/host guards, `gerbil_sam64k` Disk Extended Color BASIC 1.1 (`MEM=22823`) + `e1` `InfoPort`, `gerbil_gime` "Become CoCo 3" Super Extended BASIC 2.1 + `HSCREEN 2` PNG capture, and 6809E `centipede run-decb` preload |
| 10 turbolab | 36 | live TFR/911H `ON tfr911h RUN turbolab WITH ...`: `r/0xFFFE` + `trace_count = 15` + 64 KB `/tmp/fault.img`, `trigger = r/0xFFE8` Red-Page fault + `SWI` registers, `pi.decb` `.list` annotations + `x/0x1019/3`, repeated `WITH watch = ...`, built-in `Turbo9OS` `@kernel+0x1B` + `SWI2` `F$Link "Init"` / `RTI`, and 6309 native `LDMD #1` `BRA *` + 14-byte `D`/`W` (`E`/`F`) `NMI` register capture |

Engine-suite images (env vars, defaults under the coco-shelf checkout; a missing image **skips** that engine):
`V5_IMG_FLAT_BOOT/DISK` (`nitros9/recipes/flat65280/…`), `V5_IMG_DEEP_BOOT/DISK` (`…/deep65280/…`),
`V5_IMG_FUZIX_BIN/DISK` (no default).  Images are *copied* into a temp `/pc` (tether refuses symlinks).  Fixtures in
`tests/systests/pcfix/`; full per-engine transcripts land in `build/systest-logs/` (they include a goroutine dump
when tether times out).

Other checks: `make check-sh`, `make check-io`, `make check-core1` (strict `-s`: **0** flash calls from core-1 SRAM
functions, enforced on every `make tfr911h` and `make centipede32z`), `scripts/check-flash-size.sh` (`< 4 MB` LittleFS boundary, enforced on
every `make tfr911h` and `make centipede32z`), `scripts/elf-parity.sh`.

**Not covered by any automated test:** `gerbil_gep9` / `gerbil_io` on physical CoCo 2/3 motherboards, moving a core device
and running a guest on it.

---

## 5. Known bugs and limitations

### 5.1 Highest risk — fix or verify first
1. **Deflated zip entries need ~47 KB of heap in a few blocks** and can fail on a fragmented heap (clean error, no
   hang).  Big pak entries should be stored (`zip -0`).
2. **Hatvan on Centipede 32z (`gerbil_gep9`) is unverified on hardware** — on a real CoCo motherboard, SAM drives `E`/`Q`
   continuously at 0.89 MHz (`gerbil.pio` does not expose `BS`/`LIC` or stretch `E`/`Q` during disk VFS RPCs).

### 5.2 Known quirks (understood, not fixed)
* **Heap is small:** ~55.4 KB free after boot (`centipede heap-info`: `free=55416` on 2026-11-15 after adding Becker
  UART rings, SN76489 FIFO, InfoPort buffer, and GIME/Palette state in SRAM).  Heap fragments; a single command that
  allocates more than the 16 KB reserve at once can still abort; C++ `new` failure aborts with a message (intended).
* `on` / `run` / `ports` state is RAM only and must be re-set by Tcl every boot (by design).
* **`IoClaim` still stops the board (`ABORT`)** — only reachable by an internal error that `PortsValidate` should
  have caught.
* picotool is unusable without USB permissions (`quick-reflash` now copies directly in Go without `/bin/sh`, verifying
  `INFO_UF2.TXT` and refusing ambiguous multi-board BOOTSEL mounts).
* tether: many files fail `gofmt -l`, `go vet` says "unreachable code" at `tconsole.go:1710` — inherited from v4.
* The restart/engine suite flaked badly before S54; after the fix the last several full runs passed, but it was not
  measured over many runs.  If a spawn prints nothing, look at the goroutine dump in `build/systest-logs/`.
* Centipede label probes: at one moment the Centipede stopped answering `--quick-get-label` for ~10 minutes and
  recovered; cause unknown (we never wrote to it).
* The 30 000-distinct-variables case now gives a Tcl error; a script that makes one huge string in one command could
  still abort.
* `TCL_MEM_DEBUG=1` is on in the product build (decided "for now"; revisit before release).
* C++ identifiers/files keep the short name (`setup_multicomp09`, `engines/multicomp09.h`); `CentipedeConfig` /
  `centipede_config` has been renamed to `Config` / `g_config`.

---

## 6. Open questions for Strick (unanswered or awaiting OK)

1. **Spec §16 items (updated 2026-11-15):** R1 approved (`$FF89`, do not use `$FF15`); R2 decided (both `jamesbrown`
   with Diller's I/O addresses and `coco3` with CoCo-compatible addresses); R3 & R20 decided (`on ... run ... for ...
   with ...` lowercase grammar, `ports list|check`, `Config(engine)` and `enhance` removed); **R15 (`l2fn`) deferred
   and open to discussion**.
2. **Plain `gep9` and EMBIGGEN:** *Resolved 2026-11-15* — plain `gep9` keeps `$FF40–47` (`$FFE0–E7` on `for coco2`), now
   in `ports.tsv`, since all `gep*` machines will eventually embiggen by default.
3. **Other engine aliases:** *Resolved 2026-11-15* — all engine aliases removed; replaced by `(on, run, for)`.
4. **Rename C++ identifiers/files** that still say `multicomp09`?  And `CentipedeConfig` → `Config`?
5. **`enhance` and `ports` subcommands:** *Resolved 2026-11-15* — `enhance` removed; `ports` trimmed to `ports list` and `ports check`.
6. **Hatvan `COCO` profile (R4)** needs `strickyak/hatvan-os` (kernel *and* `gep9` emulator) built per profile; the
   embiggen base is hard-coded in ~15 places in `hatvan-os` and in minigolf (`cstart.asm` variants,
   `big6809/cstart.go`, `main.go`, `prelude.golf`, tests, and every built binary).  When, and who edits those repos?
7. **How should an automated gep9/Hatvan test work** (images, prompts)?  Needed for §5.1.
8. **`term.c` fast path** (Q14: kept for now) — revisit when the guest kit is imported.
9. **Memory budget:** is ~64 KB of heap enough for the next features (Becker/FujiNet buffers, FDC)?  Candidates to
   reclaim: the 20 KB Tcl stack array, the three `IoMap`s (1 KB each), `TCL_MEM_DEBUG`.
10. `find-board.sh` probes the Centipede on every call — acceptable, or add an `--only-tfr` path?
11. `.pak` format details (spec Appendix F) — whenever you want to work on them.
12. Turbo9OS no longer prints the stray debug character `I` at startup — fine?
13. Should `quick-reflash` stop using a shell, and should we add a udev/permissions note for picotool?

---

## 7. Next steps (suggested order)

1. **Remaining Phase 3 items:**
   * generate the guest definitions (NitrOS-9 `.d`, Go `gep9` table) from `ports.tsv`;
   * `third_party/PATCHES.md` upstream audit; Linux `openat2` (`RESOLVE_NO_SYMLINKS | RESOLVE_BENEATH`) fast path for `/pc`.
2. **Phase 4 — Centipede 32z product:** *Completed 2026-11-15* (`firmware/products/centipede32z` -> `build/centipede32z.uf2`, `boards/centipede32z.h`, 4 `IN_RAM` Gerbil engines in `firmware/engines/gerbil_guts.h`, 13 target launchers, `g_io_drive_mask`, shared library merge, flashed & verified live on the 16 KB CoCo 2 in `09_centipede_coco2_test.sh`).
3. **Phase 5 — enhancement devices:** *Completed 2026-11-15* (`becker`, `audio`, `rtc`, `orchestra90`, `info` in `firmware/lib/devices/` via repeated `with e1 with ...`; remaining: FujiNet guest kit under `os/fujinet/`).
4. **Phase 6 — TurboLab:** *Completed 2026-11-16* (unified into `tfr911h.uf2` + `build/tether` per `R21` as `on tfr911h run turbolab [with ...]`, `hamster_turbolab` in `firmware/engines/turbolab.h`, `turbolab.go` in `host/cmd/tether/`, `run_tfr911h_turbolab.sh`, verified in `10_turbolab_test.sh`).
5. **Phase 7** — docs, CI, release notes, `.pak` loader.
6. The remaining S38 item: nothing O(file size) in the Tcl `zip`/`multifile`/`edit` paths beyond the policy limits
   (acceptable as is).
7. Keep the spec current: log every finding as an `Sxx`, every deviation in §16, and update Appendix G.

---

## 8. Where things are

```
v5/
  AGENTS.md  README.md  Makefile  run_*.sh  doc/  boards/  cmake/  rc/  os/  third_party/  scripts/  tests/  host/  firmware/
  firmware/lib/core     cobs, heap.h, oom_abort.h, abort.h, config.h (Config), usb pipeline, coro, …
  firmware/lib/fs       VFS (vfs.h), littlefs glue, vfs_rpc.h
  firmware/lib/tcl      tcl_commands.h, tcl_stream.h, menu.h, pico_rpc.h, editor.h, md5.h
  firmware/lib/bus      pio_assembler.h, hamster.h
  firmware/lib/devices  iomap.h, io_install.h, ports_config.h, turbolab_config.h, enhancements.h, emudsk.h, gime.h, …
  firmware/lib/mem      deep_mmu.h, gep9_mmu.h
  firmware/engines      ports.tsv, registry.h, flat65280.h, deep65280.h, gep9.h, multicomp09.h, turbolab.h, guts.h, gerbil_guts.h
  firmware/products/tfr911h       main.cpp, background.h, repl.h, drain.h, timers.h, pins.h, ports_cmd.h, hamster.pio, …
  firmware/products/centipede32z  main.cpp, centipede_targets.h, gerbil.pio, floppy.h, gspoon.h, compress.h, …
  host/cmd/tether       the Go tether (pcfs.go = /pc confinement; pico_rpc.go; turbolab.go; tconsole.go)
  tests/host  tests/systests (common.sh, 01..10, engines/*.exp, pcfix/, testdata/)
  scripts   find-board.sh gen-ports.sh run-tfr911h-demo.sh run-centipede32z-demo.sh elf-parity.sh check-*.sh doctor.sh env.sh import-from-v4.sh
  build/    generated/ports_gen.h, io-map.md · tfr911h.uf2 · centipede32z.uf2 · tether · systest-logs/ · host-tests/   (git-ignored)
```

Tcl commands added in v5: `centipede heap-free|heap-info|heap-probe`, `on`/`run`, `ports list|check`, checked `bye`.
Environment: `scripts/env.sh` (POSIX) selects pico-sdk 2.3.0 under the shelf (ignores a stale ambient
`PICO_SDK_PATH`; override with `V5_PICO_SDK`); Go commands use `GOWORK=off CGO_ENABLED=0`.

---

## 9. Lessons learned (each one cost real time)

* **tether blocks on stdin** unless `-n`, and its serial `Close()` could block forever: a hung-looking board was
  often a hung *tool*.  Take a SIGQUIT goroutine dump before blaming hardware.
* **`malloc` never returned NULL** on the Pico SDK by default; every "graceful failure" path was dead code.
  Always test the failure path on the board, not just on the host.
* **A host test cannot see registry/name mismatches or `TCL_MEM_DEBUG` behavior** (S51, S52, the `multicomp09` vs
  `multicomp09fuzix` bug): run the hardware suite after every firmware change.
* **A Makefile keeps only the last recipe for a target** — tests silently stopped running.
* **`pkill -f PATTERN` kills your own shell** if the pattern is in your command line; use `pgrep -x tether`.
* **Don't guess devices:** `/dev/ttyACM*` renumbers on re-enumeration and with two boards a guess can hit the
  wrong one.  Use by-id names.
* **Behavior-preserving refactors:** prove them (byte-identical binary via `elf-parity.sh`, or a temporary
  old-vs-new comparison on the board, removed before the commit).
* The board keeps old `/rc` files; the Fuzix engine's old alias spellings would have silently booted Turbo9OS
  (now refused).

---

## 10. Pointers

* Spec: `doc/v5-notes-and-recommendations-two-and-a-half.md` — §7 I/O map and profiles, §13 answered questions,
  §15 decisions (incl. **15.x port configuration**), §16 revisions, Appendix F `.pak`, Appendix G implementation log
  (G.4–G.6 and the "Phase 3, step N" entries; findings **S47–S55**).
* History: `doc/v5-notes-and-recommendations-one.md` and `-two.md` (kept as history).
* Diller's `l2` is a separate engine personality (`L2__Foreground`, namespace `l2`, ACIA at `$FF14/15`, launched by
  the Tcl command `l2`) — see the answer recorded under R2; his repo is read-only reference.
