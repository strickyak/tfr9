# CoCo 3 Custom Palette Rainbow in HSCREEN 2

This guide explains how to draw a solid, multi-colored rainbow arc on the Tandy Color Computer 3 using **Extended Color BASIC**, **HSCREEN 2** (320 × 192, 16 colors), and the **`PALETTE`** command with custom hardware color codes loaded from a **`DATA`** statement.

---

## 1. BASIC Source Code (`Rainbow_hscreen2.bas`)

```basic
10 REM === COCO 3 RAINBOW WITH CUSTOM PALETTE ===
20 HSCREEN 2: HCLS
30 REM --- LOAD 7 RAINBOW COLORS FROM DATA ---
40 FOR I = 1 TO 7
50   READ C
60   PALETTE I, C
70 NEXT I
80 REM --- DRAW CONCENTRIC BANDS (ROYGBIV) ---
90 FOR B = 1 TO 7
100   FOR R = 155 - (B-1)*10 TO 155 - B*10 + 1 STEP -1
110     HCIRCLE (160, 191), R, B
120   NEXT R
130 NEXT B
140 GOTO 140
150 REM --- DATA: RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, VIOLET ---
160 DATA 7, 38, 36, 18, 31, 11, 9
```

---

## 2. Interactive One-Liner

To test immediately from the `OK` prompt or over a serial injection script:

```basic
HSCREEN 2: HCLS: FOR I=1 TO 7: READ C: PALETTE I,C: NEXT: FOR B=1 TO 7: FOR R=155-(B-1)*10 TO 155-B*10+1 STEP -1: HCIRCLE (160,191),R,B: NEXT R,B: DATA 7,38,36,18,31,11,9
```

---

## 3. How It Works

### `PALETTE <slot>, <color>`
On the CoCo 3, GIME provides 16 palette registers (`0` to `15`). Each register can be assigned any of the **64 master hardware colors** (`0` to `63`):
- `PALETTE slot, color` directly overrides that slot with the specified 6-bit color code.
- Loops lines 40–70 read custom color codes from the `DATA` statement and program palette registers `1` through `7`.
- Background remains palette slot `0` (default black or background).

### Rainbow Color Palette Codes

| Slot | Color Name | Composite Monitor Code | RGB Monitor Code | RGB Bit Pattern (`%00RRGGBB`) |
| :---: | :--- | :---: | :---: | :---: |
| `1` | **Red** | `7` | `48` | `%00110000` |
| `2` | **Orange** | `38` | `52` | `%00110100` |
| `3` | **Yellow** | `36` | `60` | `%00111100` |
| `4` | **Green** | `18` | `12` | `%00001100` |
| `5` | **Cyan** | `31` | `15` | `%00001111` |
| `6` | **Blue** | `11` | `3` | `%00000011` |
| `7` | **Violet / Magenta** | `9` | `51` | `%00110011` |

> [!TIP]
> If you are displaying output on an **RGB monitor** (or RGB palette mapping), change line 160 to:
> ```basic
> 160 DATA 48, 52, 60, 12, 15, 3, 51
> ```

---

## 4. Geometry and Screen Clipping

- **Screen Dimensions**: In `HSCREEN 2`, the screen is 320 pixels wide (`X = 0..319`) and 192 pixels tall (`Y = 0..191`).
- **Circle Center**: The arc is centered at `(160, 191)` (horizontal center, bottom edge of the display).
- **Automatic Clipping**: In CoCo 3 Extended Color BASIC, `HCIRCLE` automatically clips all points that fall outside the screen boundary. Because the center is at `Y = 191`, the lower half of the circle is clipped away, creating a clean rainbow arch across the sky.
- **Thick Solid Stripes**: The nested `FOR R` loop draws concentric 1-pixel-spaced circles (`STEP -1`) across 10 pixels of width for each band `B`, filling out solid, vibrant bands without gaps.
