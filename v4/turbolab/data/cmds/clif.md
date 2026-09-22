# CLIF - Command-Line Interpreter Forth

`clif` is a lightweight, self-contained Forth interpreter written in 6809 assembly language as an OS-9 Level 1 command module for the TFR/911h single-board computer (TurboLab SBC).

It is designed to evaluate Forth one-liners directly from the OS-9 shell prompt (`TOS:`), supporting 16-bit integers, arbitrary arithmetic, memory inspection/modification, and **interpretation-mode `DO ... LOOP`** execution outside colon definitions.

---

## 1. Features

- **Direct OS-9 Command Line Evaluation**: Evaluates commands passed as parameters on the invocation line.
- **16-bit Data Stack**: Operates on 16-bit signed and unsigned integers using the 6809 `U` stack register.
- **Interpretation-Mode `DO ... LOOP`**: Most traditional Forth implementations restrict `DO ... LOOP` to compiled colon definitions. `clif` manages loop state on a dedicated loop stack, allowing loops directly on the command line.
- **Shell-Safe Syntax (`POKE`, `STORE`, `W!`)**: In OS-9 and Turbos, the bare exclamation point `!` is reserved by the shell as the pipe operator (`/pipe`). `clif` provides aliases `POKE`, `STORE`, and `W!` (plus `PEEK`, `FETCH`, `W@` for `@`) so memory can be written and read without shell interference.
- **Quote-Transparent Tokenizer**: Double quotes (`"`) are treated as whitespace delimiters by the tokenizer. This allows standard Forth syntax containing `!` to be passed safely inside quotes (e.g. `clif "$FF00 $40 swap !"`).
- **Number Literals**: Supports decimal integers (e.g. `123`, `-45`) and hexadecimal integers prefixed with `$` (e.g. `$FF00`, `$40`). Case-insensitive for hex digits (`$A`..`$F` or `$a`..`$f`).
- **Buffered Output**: Output characters are buffered in a local scratch buffer and written via `I$Write`, terminating with an automatic newline if output was generated.

---

## 2. Vocabulary Reference

### Stack Manipulation
| Word | Stack Effect | Description |
|------|--------------|-------------|
| `DUP` | `( n -- n n )` | Duplicate top of stack |
| `DROP` | `( n -- )` | Discard top of stack |
| `SWAP` | `( n1 n2 -- n2 n1 )` | Swap top two stack items |
| `OVER` | `( n1 n2 -- n1 n2 n1 )` | Copy second item to top |
| `ROT` | `( n1 n2 n3 -- n2 n3 n1 )` | Rotate third item to top |

### Arithmetic & Logic
| Word | Stack Effect | Description |
|------|--------------|-------------|
| `+` | `( n1 n2 -- sum )` | 16-bit addition (`n1 + n2`) |
| `-` | `( n1 n2 -- diff )` | 16-bit subtraction (`n1 - n2`) |
| `*` | `( n1 n2 -- prod )` | 16-bit multiplication (`n1 * n2`) |
| `AND` | `( n1 n2 -- n3 )` | Bitwise AND |
| `OR` | `( n1 n2 -- n3 )` | Bitwise OR |
| `XOR` | `( n1 n2 -- n3 )` | Bitwise Exclusive-OR |

### Memory Access
| Word | Stack Effect | Description |
|------|--------------|-------------|
| `!` | `( n addr -- )` | Store 16-bit value `n` at `addr` |
| `POKE`, `STORE`, `W!` | `( n addr -- )` | Shell-safe aliases for `!` |
| `@` | `( addr -- n )` | Fetch 16-bit value from `addr` |
| `PEEK`, `FETCH`, `W@` | `( addr -- n )` | Aliases for `@` |
| `C!` | `( c addr -- )` | Store 8-bit byte `c` at `addr` |
| `CPOKE` | `( c addr -- )` | Alias for `C!` |
| `C@` | `( addr -- c )` | Fetch 8-bit byte from `addr` |
| `CPEEK` | `( addr -- c )` | Alias for `C@` |
| `?` | `( addr -- )` | Fetch 16-bit value at `addr` and print decimal |

### Control Flow
| Word | Stack Effect | Description |
|------|--------------|-------------|
| `DO` | `( limit index -- )` | Begin loop from `index` up to `limit` |
| `LOOP` | `( -- )` | Increment index by 1; repeat if `index < limit` |
| `I` | `( -- index )` | Push current loop index to data stack |

### Output & System
| Word | Stack Effect | Description |
|------|--------------|-------------|
| `.` | `( n -- )` | Print signed 16-bit integer in decimal followed by space |
| `EMIT` | `( c -- )` | Emit ASCII character `c` |
| `SPACE` | `( -- )` | Emit a space character |
| `CR` | `( -- )` | Emit carriage return / newline |
| `BYE` | `( -- )` | Immediately terminate execution and return to OS-9 |

---

## 3. Tested Hardware Examples

All examples below were tested live on the physical TFR/911h hardware running Turbos:

### Basic Arithmetic and Printing
```console
TOS: clif 0 .
0 

TOS: clif 1 .
1 

TOS: clif 123 .
123 

TOS: clif 2 3 + .
5 

TOS: clif 10 20 + .
30 

TOS: clif 5 5 * .
25 
```

### Interpretation-Mode `DO ... LOOP`

#### Loop Counter (`I`)
```console
TOS: clif 5 0 DO I . LOOP
0 1 2 3 4 
```

#### Doubling Loop (`I I + .`)
```console
TOS: clif 5 0 DO I I + . LOOP
0 2 4 6 8 

TOS: clif 20 0 DO I I + . LOOP
0 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 32 34 36 38 
```

#### Squaring Loop (`I I * .`)
```console
TOS: clif 5 0 DO I I * . LOOP
0 1 4 9 16 

TOS: clif 20 0 DO I I * . LOOP
0 1 4 9 16 25 36 49 64 81 100 121 144 169 196 225 256 289 324 361 
```

### Memory Operations (`POKE` / `PEEK` / `!` / `@`)

#### Using Shell-Safe Aliases (`poke`, `peek`)
```console
TOS: clif $FF00 $40 swap poke
 
TOS: clif $FF00 peek .
2560 
```

#### Using Quoted Forth Syntax (`!`, `@`)
Quotes protect `!` from the OS-9 shell pipe interpreter:
```console
TOS: clif "$FF00 $40 swap !"
 
TOS: clif $FF00 @ .
2560 
```

### Character I/O (`EMIT`, `CR`)
```console
TOS: clif 65 EMIT 66 EMIT CR
AB
```

---

## 4. Building and Running

### Assemble with `lwasm`
From `turbolab/data/cmds/`:
```bash
/home/strick/modoc/coco-shelf/bin/lwasm --6809 --format=os9 \
    --pragma=pcaspcr,nosymbolcase,condundefzero,undefextern,dollarnotlocal,noforwardrefmax \
    -I /home/strick/modoc/coco-shelf/nitros9/defs \
    -o clif clif.asm
```

### Launch on Hardware via Tether
Pass `clif` as an extra module appended to the primordial ROM kernel:
```bash
build/tether data/turbos/turbos_dev.img.rom data/cmds/clif
```
Once the `TOS:` shell prompt appears, `clif` is immediately available in the OS-9 execution directory.
