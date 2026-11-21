# `jamesbrown` FujiNet & `foggynet` Guide

This guide covers using FujiNet (on real TFR/911H hardware via `run-tfr911h-jamesbrown.sh`, or simulated on a PC via `run-gep9-jamesbrown.sh` + `foggynet`), finding and mounting OS-9 `.dsk` images, and running the bundled FujiNet and diagnostic commands in NitrOS-9 Level 2.

---

## 1. Host Slots vs. Device Slots

* **Host slots (`0`–`7`):** Where disk images live.
  * Slot `0` defaults to `SD`:
    * On real TFR/911H hardware: the microSD card on the ESP32 FujiNet hat.
    * Under `./run-gep9-jamesbrown.sh` (`foggynet`): `build/demo-pc/jamesbrown-gep9/foggynet/sd/` on your host PC.
  * Slots `1`–`7` hold TNFS server hostnames (`tnfs.fujinet.online` is preloaded in slot `1`; `apps.irata.online` hosts a large CoCo OS-9 collection). Both real `fujinet-firmware` and `foggynet` try **TCP port 16384 first** and fall back to UDP (`_tcp.` or `_udp.` prefixes force one).
  * You can also run the bundled read-only TCP TNFS server (`build/tnfsd --fs=/some/dir --listen=localhost:16384`) or mount a TCP TNFS server on Linux via FUSE (`build/tnfsfuse --server=apps.irata.com --mount=/tmp/tnfs`).
* **Device slots (`0`–`3`):** Map 1-to-1 to NitrOS-9 DriveWire RBF drives **`/x0`**, **`/x1`**, **`/x2`**, **`/x3`**.
  * Use slots `1`–`3` (`/x1`–`/x3`) when mounting extra disks (`fnconfig` protects `/x0` from interactive overwrite in case `/x0` is a DriveWire root filesystem).
  * Note: NitrOS-9 `dir /x1` expects an **OS-9 RBF** `.dsk` image (usually marked `(OS-9)` on TNFS archives), not a CoCo Disk BASIC (DECB) `.dsk`.

---

## 2. Finding & Mounting an OS-9 `.dsk` Interactively (`fnconfig`)

There is no standalone command-line `fnls`; use `fnconfig`'s built-in directory browser to explore a TNFS server or `SD`:

1. At the `OS9:` prompt, run:
   ```text
   fnconfig
   ```
2. **Add & mount a TNFS host:**
   * Press **`2`** (*Hosts*), arrow down to slot **`2`**, press **`E`** (*edit*), type `apps.irata.online`, and press **Enter**.
   * Press **`M`** to mount host slot `2`.
3. **Browse directories & pick an OS-9 `.dsk`:**
   * Press **`3`** (*Devices*), arrow **Down** to **`/x1`** (slot `1`), and press **`B`** (*browse*).
   * At the prompts, enter host slot **`2`** and mode **`R`** (read-only).
   * Navigate with **Up/Down** and **Enter** (`N` = next page, `B` = parent directory):
     * e.g. `COCO/` -> `Operating Systems/` (or `Applications/`, `Utilities/` — look for `(OS-9)` in folder names) -> `Multi-Vue icons (OS-9)/` -> select `vmicons.dsk` with **Enter**.
4. Press **`Q`** to exit `fnconfig`, then inspect the mounted disk in NitrOS-9:
   ```text
   dir /x1
   info
   ```
   *(Tip: `fngetdevfile 1` prints the full `/COCO/...` path currently assigned to slot `1` so you can script it later.)*

---

## 3. Scripting Mounts from the `OS9:` Command Line

Once you know the server and path, the CLI commands (`fnsethost`, `fnlisthosts`, `fnmount`, `fnsetdevfile`, `fnmountimg`, `fnlistdevs`, `fngetdevfile`) perform the entire workflow directly (`mode`: `1` = read-only, `2` = read/write).

### Example A: Mount a Remote OS-9 `.dsk` over TNFS onto `/x1`

```text
* 1. Point host slot 2 at apps.irata.online and mount the host
fnsethost 2 apps.irata.online
fnlisthosts
fnmount 2

* 2. Assign device slot 1 (/x1) -> host slot 2, mode 1 (RO), path (spaces need no quotes)
fnsetdevfile 1 2 1 /COCO/Operating Systems/Multi-Vue icons (OS-9)/vmicons.dsk

* 3. Mount the image in device slot 1 (mode 1 = RO) and verify
fnmountimg 1 1
fnlistdevs
fngetdevfile 1

* 4. Read it as a standard NitrOS-9 RBF disk
dir /x1
```

### Example B: Mount a Local `.dsk` from Your PC (`SD` Host Slot `0`) onto `/x1` Read/Write

1. Under `./run-gep9-jamesbrown.sh`, copy any OS-9 `.dsk` into `foggynet`'s `SD` directory on your host PC:
   ```sh
   mkdir -p build/demo-pc/jamesbrown-gep9/foggynet/sd
   cp build/tfr911-jamesbrown.dsk build/demo-pc/jamesbrown-gep9/foggynet/sd/extra.dsk
   ```
2. At the `OS9:` prompt:
   ```text
   fnmount 0
   fnsetdevfile 1 0 2 /extra.dsk
   fnmountimg 1 2
   dir /x1/cmds
   ```

---

## 4. Bundled FujiNet & System Commands in `/DD/CMDS`

| Command | Description |
|---|---|
| `fnstatus` | Show FujiNet firmware/adapter version, WiFi status (`simulated` under `foggynet`), and IP settings |
| `fnconfig` | Full-screen TUI for adapter status, host slots, directory browsing, disk mounting, and WiFi |
| `fnlisthosts`, `fngethost`, `fnsethost`, `fnmount` | Inspect, edit, and mount FujiNet host slots `0`–`7` |
| `fnlistdevs`, `fngetdevfile`, `fnsetdevfile`, `fnmountimg` | Inspect, assign, and mount DriveWire disk slots `0`–`3` (`/x0`–`/x3`) |
| `fnweather` / `fnwx` | Live weather & 8-day forecast (`ip-api.com` + Open-Meteo HTTPS JSON) |
| `fnnews` | 80-column FujiNet 8-Bit News reader (`fujinet.online`) |
| `fnstocks` | Live stock quotes, symbol lookup, and persistent watchlist |
| `fnbounce` | Bounce World multi-system networked shape simulation client |
| `fnbattleship` | FujiNet multiplayer/AI Battleship client (`battleship.carr-designs.com`) |
| `info` | NitrOS-9 system/memory/process/device/disk/module report + `$FF43`–`$FF44` hardware InfoPort |
