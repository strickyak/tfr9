# `tether` Terminal Modes, Newline Translation, and `^C` / `^]` Strategy

This document records the design, implementation details, and rejected alternatives for console I/O across `tether`, the `run_tfr911h_*.sh` launchers (especially `run_tfr911h_gep9_hatvan.sh`), and Hatvan OS `/term`.

---

## 1. Problem Statement

Interactive console sessions over `tether` bridge a POSIX host terminal (`os.Stdin` / `os.Stdout`) to a 6809/6309 guest OS over COBS packets (`inkey` RX bytes and `C_PUTCHAR` TX bytes). Four distinct tensions arise:

1. **Line cooking & echo ownership:**
   * In single-character (`cbreak`) mode, host `echo` is off (`stty -echo`), so the **guest OS** must echo typed characters, process Backspace (`BS`/`DEL`), and echo `\r\n` on Enter.
   * In host-cooked (`-cooked`) or host-readline (`-readline`) mode, the **host** already displays and edits the line before sending it on Enter; if the guest *also* echoes the line, every command prints twice unless reconciled.
   * When `stdin` is disabled (`-n` / `-nostdin`), `tether` must not read `stdin` **or** alter the caller's terminal `stty` state.
2. **Mixed newline conventions in guest userlands:**
   * Legacy OS-9 programs (`LineMode == 1`: `echo`, `dir`, `procs`, `tmode`) terminate lines with **`CR` (`\r`, `0x0D`)**. Sent raw to a modern terminal without `LF` (`0x0A`), subsequent output overwrites the same line.
   * Unix- and Golf-derived programs (`LineMode == 0`: `GECHO`, `GCAT`, `GDIR`, `sh`) terminate lines with **`NL` (`\n`, `0x0A`)**. Sent raw to a hardware UART (or terminal with `-opost`), `LF` without `CR` staircases across the screen.
   * Full-screen / visual editors and binary protocols need **untranslated** output bytes.
3. **Interrupt (`^C`) vs. session exit (`^]`):**
   * Users expect `^C` (`0x03`) to interrupt/cancel commands **inside the guest OS** rather than killing `tether`, with **`^]` (`0x1D`, telnet-style)** reserved to exit `tether` — except in `-nostdin` (`-n`) mode where `stdin` is not read, so `^C` (`SIGINT`) must terminate `tether`.

---

## 2. Summary of the Four Terminal Modes

`scripts/run-tfr911h-demo.sh` normalizes shorthand flags (`-c`/`-cooked`, `-r`/`-readline`, `-n`/`-nostdin`) for all `run_tfr911h_*.sh` scripts, and `run_tfr911h_gep9_hatvan.sh` (like `run_tfr911h_jamesbrown.sh`) enables `-pass_ctrl_c` by default:

| Launcher Flag | `tether` Flag | Host `stty` (when TTY) | Line Cooking & History | Character Echo | `^C` (`0x03`) | `^]` (`0x1D`) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| *(default)* | *(default)* `-pass_ctrl_c` | `cbreak -echo -ixon -isig` | Guest OS (`TermReadLn` / SCF) | Guest OS (`PD.EKO=1`) | Sent immediately to guest | Quits `tether` (`0`) |
| `-c`, `-cooked` | `-cooked -pass_ctrl_c` | `icanon echo -isig eol '^C' eol2 '^]'` | Linux kernel `n_tty` | Linux TTY (+ `EchoSuppressor`) | Flushes via `VEOL`, discards partial line, sends `0x03` to guest | Flushes via `VEOL2`, quits `tether` (`0`) |
| `-r`, `-readline` | `-readline -pass_ctrl_c` | `cbreak -echo -ixon -isig` | `tether` `ReadlineEditor` (`~/.tether_history`) | `tether` (+ `EchoSuppressor`) | Cancels draft, sends `0x03` to guest | Quits `tether` (`0`) |
| `-n`, `-nostdin` | `-n -pass_ctrl_c` | *Untouched* | None (`stdin` ignored) | N/A | **Host `SIGINT` kills `tether`** | *(not read)* |

*(Note: if `-pass_ctrl_c` is omitted on a raw `tether` invocation, `^C` remains normal host `SIGINT` in all modes.)*

---

## 3. Implementation Details

### 3.1 Guest Strategy: Hatvan OS `/term` (`WritLn` vs. `Write` and `SS.Opt`)

Hatvan OS follows the classic Microware OS-9 **SCF (`Sequential Character File Manager`)** contract (`hatvan-os/kernel/common/{dev,path,sh,syscall}.golf`):

1. **`I$WritLn` (`SysWritLn`) performs CRLF expansion; `I$Write` (`SysWrite`) stays raw:**
   * In `path.golf` (`SysWritLn`), lines stop at `\r` when `LineMode == 1` (OS-9) or at `\r` / `\n` when `LineMode == 0` (Unix/Golf).
   * When writing to `DEV_TERM` (with `PD.ALF != 0`) or `DEV_LOG`, if the line ends in **either** `\r` or `\n`, `SysWritLn` rewrites the terminator in `pathLineBuf` (`[258]byte`) to `\r\n` before calling `SysWrite`, then restores `TransferredBytes = origLen` so register `Y` still reports the exact byte count from the caller's buffer.
   * Disk files (`DEV_DISK`) and `SysWrite` (`I$Write`) are **never** modified, preserving exact byte streams for file redirection (`ECHO foo > file`) and raw terminal control / visual editors.
   * Built-in kernel shell messages in `sh.golf` also emit `\r\n`, and the host Go VMs (`gep9`, `gepk`, `gep5`, `gep0`) track `lastTermCR` on `$FF00` (`Term.Out`) so `\r\n` collapses to a single `\n` on standard output.
2. **32-byte OS-9 SCF `PD.OPT` table & `tmode` support (`TermInit`, `TermReadLn`):**
   * `dev.TermOpt` (`[32]byte`) initializes standard SCF offsets at boot: `PD.BSO=1` (`[2]`), `PD.EKO=1` (`[4]`), `PD.ALF=1` (`[5]`), `PD.PAG=24` (`[8]`), `PD.BSP=8` (`[9]`), `PD.DEL=24` (`[10]`), `PD.EOR=13` (`[11]`), `PD.EOF=4` (`[12]`), `PD.INT=3` (`[16]`), `PD.QUT=5` (`[17]`), `PD.BSE=8` (`[18]`), `PD.OVF=7` (`[19]`).
   * `I$GetStt` and `I$SetStt` handle `SS.Opt` (`B == 0x00`) on `DEV_TERM`, allowing standard OS-9 `/d0/Cmds9/tmode` to inspect and modify terminal flags (`eko`, `alf`, `upc`, `bso`, etc.).
   * `TermReadLn` honors `TermOpt`: echoes printable characters when `PD.EKO != 0`, erases on `BS`/`DEL` (`0x08`/`0x7F`) with `\b \b`, erases the whole input line on `^U`/`^X`, echoes `\r\n` on Enter, and on `^C` (`0x03` / `PD.INT`) echoes `^C\r\n`, discards any partial line, and returns an empty line to the caller.

### 3.2 Host Strategy: `tether` Console Modes (`host/cmd/tether/`)

1. **Preventing `stty` corruption under `-n` (`tconsole.go`):**
   * `SaveSttyState()` / `SetStty*()` are gated on `!*NO_KEYBOARD`, so `-nostdin` (`-n`) never alters the caller's terminal settings.
2. **`EchoSuppressor` for `-readline` and interactive `-cooked` (`readline.go`, `tconsole.go`):**
   * When `ReadlineInkeyRoutine` (or `CookedInkeyRoutine` when `isStdinTTY()` is true) submits a line, the user has *already* seen `line + "\r\n"` on their local terminal.
   * Before streaming the line bytes to `inkey`, `tether` records `append([]byte(line), '\r', '\n')` in `lineEchoSuppressor` with a 2-second TTL.
   * Incoming `C_PUTCHAR` bytes matching the front of `lineEchoSuppressor` are dropped once. On the very first non-matching byte (for example, if the guest has `PD.EKO == 0` and immediately emits command output) or after TTL expiry, the expected buffer clears immediately so zero real output bytes are lost.
   * When `-cooked` reads from a **pipe or file** (`!isStdinTTY()`), `EchoSuppressor` is not armed, preserving the echoed command in automated test logs and transcripts.
3. **Prompt-preserving `ReadlineEditor` (`readline.go`):**
   * `ReadlineEditor` tracks only `renderedLen` and `renderedPos` (offsets relative to the cursor position where editing began, right after whatever prompt the guest printed) and redraws using relative `\b` movements + trailing space-wipe when lines shrink.
   * Supported keys: `←`/`→` (`^B`/`^F`), `↑`/`↓` (`^P`/`^N`) with in-progress draft preservation and persistent `~/.tether_history`, `Home`/`End` (`^A`/`^E`), word motion (`Alt-B`/`Alt-F`, `Ctrl-←`/`Ctrl-→`), `BS`/`DEL`, `Delete` (`ESC [ 3 ~` / `^D` on non-empty line), `^D` EOF on empty line, kill/yank (`^K`, `^U`/`^X`, `^W`, `Alt-D`, `^Y`), transpose (`^T`), clear-screen redraw (`^L`), local `~cmd` escapes (`~~` for literal `~`), `^C` (`0x03`), and `^]` (`0x1D`).
4. **Immediate `^C` and `^]` in `-cooked -pass_ctrl_c` (`utility.go`, `tconsole.go`):**
   * `SetSttyCookedPassCtrlC()` runs `stty -isig eol '^C' eol2 '^]'` on interactive TTYs (restored by `RestoreSttyState()` on exit/signal/panic).
   * Canonical (`icanon`) kernel line editing remains active for normal typing, while POSIX `VEOL` (`^C`) and `VEOL2` (`^]`) cause the kernel TTY driver to flush `0x03` or `0x1D` to `os.Stdin` immediately without waiting for Enter.
   * `CookedInkeyRoutine` reads via `bufio.Reader.ReadByte()`: on `0x1D` it restores `stty` and exits `0`; on `0x03` it discards any buffered partial line, emits `\r\n`, arms `EchoSuppressor` for `"^C\r\n"`, and sends `0x03` to the guest.

---

## 4. Rejected Alternatives

1. **Translating `\r` or `\n` inside `SysWrite()` (`I$Write`) or unconditionally in `tether`'s `C_PUTCHAR`:**
   * *Why rejected:* Any unconditional byte translation in `Write()` or in `tether`'s output path corrupts raw cursor positioning sequences, full-screen visual editors (`ded`, `umed`, `vi`), and binary serial protocols. Handling `\r` / `\n` → `\r\n` strictly in `SysWritLn()` (`I$WritLn`) matches real OS-9 SCF (`PD.ALF`) and keeps `Write()` 100% transparent.
2. **Disabling guest echo (`PD.EKO=0`) via a boot config bit or injected `tmode` command when `-cooked` or `-readline` is selected:**
   * *Why rejected:* Couples guest boot configuration to host `tether` flags, breaks piped `-cooked` runs (`printf 'dir\n' | ... -c`) where Linux does not echo and the transcript relies on guest echo, and would not generalize to unmodified NitrOS-9 or Fuzix images. Host-side `EchoSuppressor` works identically across Hatvan OS, NitrOS-9 Level 1/2, `jamesbrown`, and Fuzix, and self-clears on mismatch if `PD.EKO == 0`.
3. **Rewinding the cursor with `\b` on Enter in `-readline` mode (letting the guest re-print the line) instead of printing `\r\n` + `EchoSuppressor`:**
   * *Why rejected:* ASCII `\b` cannot move back up across terminal line wraps on commands wider than the terminal, leaving orphaned characters on the upper row; also, if the guest has echo disabled (`tmode eko=0`), erasing the local line on Enter would make the typed command disappear completely.
4. **Redrawing from column 0 (`\r`) in `-readline` mode or adding a heavy external readline dependency:**
   * *Why rejected:* `tether` is guest-agnostic and does not know what prompt string (`$ `, `OS9: `, `....hatvan% `, etc.) precedes the cursor, so `\r` would overwrite the guest's prompt. A ~300-line stdlib `ReadlineEditor` using relative cursor offsets preserves arbitrary prompts and keeps `host/go.mod` minimal and cross-platform.
5. **Using `stty intr '^]'` (`VINTR` → `SIGINT`) for `-cooked -pass_ctrl_c`:**
   * *Why rejected:* Tested on Linux PTYs and rejected because `VINTR` only delivers `SIGINT` to a foreground process group attached to a controlling terminal; in PTY test harnesses or subshells without `TIOCSCTTY`, `n_tty` consumes `^]` without signaling `tether`. Using `stty -isig eol '^C' eol2 '^]'` delivers both `0x03` and `0x1D` directly in the `stdin` byte stream in all TTY environments while keeping canonical line editing intact.
