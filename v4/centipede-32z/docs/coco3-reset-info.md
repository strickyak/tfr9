# CoCo 3 Reset Sequence & Vector Architecture

## 1. Reset Vectors: CoCo 2 vs CoCo 3

| Machine | Hardware Vector (`$FFFE-$FFFF`) | SAM Compatibility Vector (`$BFFE-$BFFF`) | Primary Function |
| :--- | :--- | :--- | :--- |
| **CoCo 2** | `$A027` | `$A027` | Extended Color BASIC 1.1 Reset Vector |
| **CoCo 3** | **`$8C1B`** | **`$8C1B`** | Super Extended BASIC 2.0 Entry Point |

In `coco3.rom` (the 32KB Super Extended BASIC ROM mapped at `$8000–$FFFF`), the 6809 hardware reset vectors point to **`$8C1B`**:

```asm
BFFE 8C1B        FDB     L8C1B        ; SAM compatibility Reset Vector (coco3.asm:08774)
FFFE 8C1B        FDB     L8C1B        ; Hardware 6809 Reset Vector    (coco3.asm:12435)
```

## 2. CoCo 3 Boot Sequence from `$8C1B`

When a CoCo 3 resets, execution begins at `$8C1B`:

```asm
8C1B 1A50        ORCC    #$50         ; Disable IRQ and FIRQ interrupts
8C1D 860A        LDA     #$0A
8C1F B7FF90      STA     >INIT0       ; GIME: 32K internal ROM, MC3=1, MMU disabled
8C22 7FFFDE      CLR     >ROMCLR      ; Enable ROM mode (SamTyBit = 0)
8C25 7EC000      JMP     >SC000       ; Jump to CoCo 3 Super Extended BASIC initialization
```

### Step-by-Step Execution:

1. **Stack & GIME Hardware Init (`$C000`)**:
   - Initializes CPU Stack Pointer: `LDS #$5EFF` (`coco3.asm:08777`).
   - Sets all 16 palette registers (`$FFB0–$FFBF`) to green composite / indigo RGB (`$12`).
   - Loads MMU Task 0 & Task 1 tables (`$FFA0–$FFAF`) from `MMUIMAGE` (`$C246`: `38 39 34 3B 3C 3D 3E 3F`).
   - Enables MMU: `STA $FF90` with `#$CE` (`COCO + MMUEN + MC3 + MC2 + MC1`).

2. **Relocate Boot Code to RAM (`$4000`)**:
   - Copies initialization routines `BEGMOVE..ENDMOVE` (`$C03F..$C36C`) into RAM at `$4000..$432D`.
   - Executes `JMP $4000` to run from RAM while manipulating ROM/RAM mapping.

3. **Cold Start vs Warm Start Check**:
   - Initializes PIA0 (`$FF00`), PIA1 (`$FF20`), and video control registers (`$FF98–$FF9E`).
   - Reads `INT.FLAG` at `$FEED` (Block 15) and `RSTFLG` at `$0071`.
   - If `INT.FLAG != $55` or warm start checks fail, enters **Cold Start**.

4. **ROM-to-RAM Copy Loop (`SC1AA`)**:
   - Calls `SC1AA` to copy `$8000–$BFFF` from ROM to RAM.
   - For each 8-byte chunk, toggles between ROM mode (`CLR $FFDE`, `SamTyBit = 0`) to read from ROM and RAM mode (`CLR $FFDF`, `SamTyBit = 1`) to write into RAM.
   - If no external Disk Basic ROM ('DK') is present at `$C000`, branches to `$C137` and copies `$E000–$FDFF` into RAM.
   - Copies interrupt vector jump table from `INTIMAGE` to `$FEED–$FEFF`.
   - Sets `CLR $FFDF` permanently (`SamTyBit = 1`, all-RAM mode).

5. **Screen Clear & Jump to BASIC Core**:
   - Clears physical 32×16 VDG video RAM (`$0400–$05FF`) with green spaces (`$60`).
   - Executes `JMP RESVEC` (`$A027`) in RAM (`coco3.asm:08982`).

6. **Color BASIC Direct-Page Setup (`$A027`)**:
   - Reconfigures PIAs, tests RAM size, clears low RAM (`$0000–$03FF`), sets `TOPRAM` (`$7FFF`), and initializes BASIC vectors.
   - Prints title banner:
     ```text
     EXTENDED COLOR BASIC 2.0
     COPYRIGHT (C) 1986 TANDY CORP.
     LICENSED FROM MICROSOFT
     OK
     ```

## 3. Why Jumping Directly to `$A027` Fails on CoCo 3

* `$A027` is the **CoCo 2** hardware reset vector.
* In CoCo 3, jumping directly to `$A027` completely bypasses:
  1. Setting `S = $5EFF` (stack remains in uninitialized/I/O space).
  2. MMU table initialization at `$FFA0–$FFAF` and MMU activation (`MMUEN = 1`).
  3. Relocation of Color BASIC and Super Extended BASIC from ROM to RAM.
  4. Clearing of the physical VDG text screen and palette initialization.
* Centipede must always launch CoCo 3 via `Jump(0x8C1B)`.
