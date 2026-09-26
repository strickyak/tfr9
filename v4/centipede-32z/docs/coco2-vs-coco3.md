# Hardware Architecture & I/O Comparison: Tandy Color Computer 2 vs. Color Computer 3

## 1. Executive Summary & Project Goal

This document provides a comprehensive technical comparison between the **Radio Shack / Tandy Color Computer 2 (CoCo 2)** and **Color Computer 3 (CoCo 3)**. It is written specifically to support using the **Centipede Board** (an RP2350B-based expansion device connected to the 40-pin cartridge port) to enhance a CoCo 2 so that it functionally behaves like a CoCo 3.

### 1.1 The Centipede Approach to CoCo 3 Emulation
A stock CoCo 2 is limited by an MC6847 Video Display Generator (32×16 text, 4-color low-res graphics), an MC6883 Synchronous Address Multiplexer (SAM), and at most 64KB of dynamic RAM. By contrast, a CoCo 3 incorporates the **GIME** (Graphics, Interrupt, and Memory Enhancement) ASIC, enabling 512KB RAM, an 8-slot Memory Management Unit (MMU), 40/80-column text, 16-color high-resolution graphics, programmable palette registers, and programmable hardware timers.

Because the CoCo cartridge port exposes the complete MC6809E bus ($A_0$–$A_{15}$, $D_0$–$D_7$, $R/\overline{W}$, $E$, $Q$, interrupts, and the **$\overline{\text{SLENB}}$** control line):
* Centipede monitors every 6809 bus cycle using high-speed RP2350B PIO state machines (`gerbil.pio`).
* Centipede asserts $\overline{\text{SLENB}}$ (Single Line Enable / Slot Enable) to instantly disable internal motherboard RAM/ROM address decoders and bus buffers.
* Centipede can substitute its own onboard 520KB SRAM/flash to emulate the **512KB physical RAM**, the **GIME MMU** ($FFA0$–$FFAF$), **CoCo 3 ROMs**, **GIME timers/interrupts**, and **floppy controller ($FF40$)**, turning the host CoCo 2 into a CoCo 3 capable of running NitrOS-9 Level 2 and CoCo 3 software.

---

## 2. Color Computer 2 (CoCo 2) Architecture

```
               +-------------------------------------------+
               |           Motorola MC6809E CPU            |
               |             (0.895 / 1.79 MHz)            |
               +---------------------+---------------------+
                                     |
                Address / Data / R/W |
                                     v
+-----------------------+  Multiplexed  +-----------------------+
|  MC6883 / SN74LS783   |     Addr      |  Motherboard RAM      |
|  SAM (Address Mux)    |-------------->|  (16KB or 64KB DRAM)  |
+-----------------------+               +-----------------------+
        |            |                              ^
Display |    Device  | Selects                      | Video Data
Addr    |    Select  v                              v
        |    +--------------------+     +-----------------------+
        |    | PIA 0 & PIA 1      |     |  MC6847 VDG           |
        |    | ($FF00, $FF20)     |     |  (32x16 Text, PMODEs) |
        |    +--------------------+     +-----------+-----------+
        v                                           | Composite/RF
+-------------------------------------------------+ |
| Cartridge Slot (40-Pin Edge Connector)          | v
| (A0-A15, D0-D7, SLENB*, CART*, CTS*, SCS*, etc) | Monitor / TV
+-------------------------------------------------+
```

### 2.1 CPU & Clocks
* **Microprocessor**: Motorola MC6809E (8-bit data bus, 16-bit address bus).
* **Clock Frequency**:
  * Nominal: **0.895 MHz** (derived from 14.31818 MHz master crystal divided by 16 by the SAM).
  * High-Speed Mode: **1.79 MHz** (software selectable by writing to SAM register `$FFD9`).
  * *Constraint*: In the CoCo 2, accessing dynamic RAM while in 1.79 MHz mode corrupts or "blanks" the MC6847 display because the SAM cannot interleave CPU and VDG RAM access cycles at 1.79 MHz.

### 2.2 Synchronous Address Multiplexer (SAM, MC6883 / SN74LS783 / 785)
The SAM manages memory cycles, dynamic RAM refresh, clock generation, and peripheral chip selects:
* **DRAM Refresh**: Inserts 40-pin transparent refresh cycles during display blanking intervals.
* **VDG Display Address Counters**: Counters $F_0$–$F_6$ and $V_0$–$V_2$ generate video RAM fetch addresses.
* **Page Select Bit ($P_1$)**: Controlled by writes to `$FFD4` (clear) and `$FFD5` (set). In 64KB mode, swaps which 32KB half of RAM appears in the lower address space.
* **Memory Map Type Bit ($TY$)**:
  * `$FFDE` ($TY=0$): **Standard Mode** — Lower 32KB is RAM ($0000–$7FFF), $8000–$BFFF is BASIC ROM, $C000–$FEFF is Extended BASIC / Cartridge ROM, and $FF00–$FFFF is I/O / Vectors.
  * `$FFDF` ($TY=1$): **All-RAM Mode** — Full 64KB RAM mapped from $0000 to $FEFF; $FF00–$FFFF remains dedicated to I/O and hardware vectors.

### 2.3 Video Display Generator (VDG, Motorola MC6847 / MC6847T1)
* **Text Mode**:
  * Resolution: 32 columns × 16 rows (512 screen bytes).
  * MC6847: Uppercase alphanumeric only (lowercase characters display in inverted video).
  * MC6847T1: Adds native lowercase characters with descenders.
  * Colors: Green or Buff background with black text.
* **Semigraphics Modes**:
  * Modes: SG4 (8 colors, 64×32), SG6 (4 colors, 64×48), SG8, SG12, SG24.
* **Graphics Modes (PMODEs)**:
  * PMODE 0: 128 × 96, 2 colors (1,536 bytes).
  * PMODE 1: 128 × 96, 4 colors (3,072 bytes).
  * PMODE 2: 128 × 192, 2 colors (3,072 bytes).
  * PMODE 3: 128 × 192, 4 colors (6,144 bytes).
  * PMODE 4: 256 × 192, 2 colors (6,144 bytes).
* **Color Palettes**: Fixed 2-color and 4-color palettes determined by VDG pin `CSS` (Color Set Select) and mode pins.

### 2.4 Keyboard & I/O
* **Keyboard**: 53-key matrix. No CTRL, ALT, F1, F2, or standalone cursor arrow keys (uses Shift + key combinations).
* **PIAs**: Two MC6821 Peripheral Interface Adapters handle keyboard, cassette, RS-232 bit-banging, analog comparator (joysticks), 6-bit DAC audio, and VDG control pins.

---

## 3. Color Computer 3 (CoCo 3) Architecture

```
               +-------------------------------------------+
               |           Motorola MC6809E CPU            |
               |             (0.895 / 1.78 MHz)            |
               +---------------------+---------------------+
                                     |
                Address / Data / R/W |
                                     v
+----------------------------------------------------------+
|  GIME ASIC (SC81471 / SC84447)                           |
|  - Integrated MMU: Dual 8-slot Task Maps (FFA0-FFAF)     |
|  - 512KB Physical RAM Addressing                         |
|  - High-Res Video Controller: 40/80-col text, 640x200    |
|  - 16 Programmable Palettes (FFB0-FFBF) from 64 colors   |
|  - Programmable 12-bit Timer & Interrupt Controller      |
|  - Constant RAM at $FE00-$FEFF                           |
+----------------------------+-----------------------------+
                             |
             +---------------+---------------+
             |                               |
             v                               v
+-----------------------+        +-----------------------+
|  512KB Physical RAM   |        |  Analog RGB &         |
|  (64 x 8KB Pages)     |        |  Composite Video Out  |
+-----------------------+        +-----------------------+
```

### 3.1 CPU & Clocks
* **Microprocessor**: Motorola MC6809E @ 0.895 MHz or 1.78 MHz.
* **Dual Speed Operation**: The GIME interleaves RAM access between CPU and video generation, permitting the 6809E to run continuously at **1.78 MHz** without video noise, snow, or display blanking.

### 3.2 The GIME Chip (Graphics, Interrupt, and Memory Enhancement)
The GIME replaces the SAM (MC6883), VDG (MC6847), and dozens of discrete 74LS TTL chips in an 84-pin PLCC package.

#### 3.2.1 Memory Management Unit (MMU)
* **Physical Address Space**: 512KB (divided into 64 physical blocks of 8KB each, numbered `$00` through `$3F`).
* **Logical Address Space**: 64KB (divided into 8 logical slots of 8KB each: $0000, $2000, $4000, $6000, $8000, $A000, $C000, $E000).
* **Dual Task Mappings**:
  * **Task 0**: Bank registers at `$FFA0`–`$FFA7`.
  * **Task 1**: Bank registers at `$FFA8`–`$FFAF`.
  * Selection between Task 0 and Task 1 is controlled by `$FF91` bit 0.
* **MMU Enable**: Bit 6 of `$FF90` ($0=\text{disabled}$, $1=\text{enabled}$).
* **Constant RAM at `$FE00`–`$FEFF`**: When enabled ($FF90$ bit 3), addresses `$FE00`–`$FEFF` (and the non-I/O portions of `$FF00`–`$FFFF`) are always permanently mapped to physical block `$3F`, regardless of which 8KB block is mapped to `$E000`–`$FFFF`. This critical feature allows OS kernels (such as NitrOS-9 Level 2) to maintain uninterrupted vector and kernel state while switching user task maps.

#### 3.2.2 Advanced Video & Graphics Engine
* **Text Modes**:
  * 32, 40, 64, or 80 columns wide.
  * 24, 25, or 28 lines high (programmable scanlines per character row: 8, 9, 10, or infinite).
  * 8×8 pixel character matrix with built-in uppercase, lowercase, and true descenders.
  * Optional per-character attribute byte: Blinking, Underline, and 8 foreground / 8 background colors.
* **Graphics Modes**:
  * Resolutions: 160, 256, 320, or 640 pixels horizontally by 192, 200, or 225 scanlines vertically.
  * Color Depths: 2 colors (1 bpp), 4 colors (2 bpp), or 16 colors (4 bpp).
  * Maximum standard mode: **640 × 200 at 4 colors** or **320 × 200 at 16 colors**.
* **Palette Registers (`$FFB0`–`$FFBF`)**:
  * 16 programmable registers selecting from a master palette of **64 colors**.
  * Outputs to Composite Video (6-bit composite color encoding) and analog RGB (6-bit RGB: 2 bits red, 2 bits green, 2 bits blue).
* **Hardware Scrolling**:
  * Vertical Fine Scroll (`$FF9C`): 0–7 scanlines smooth scrolling.
  * Vertical Offset Registers (`$FF9D` MSB, `$FF9E` LSB): Points video display origin to any byte boundary in 512KB RAM.
  * Horizontal Fine Scroll (`$FF9F`): Smooth pixel scroll.

#### 3.2.3 Programmable Timer & Advanced Interrupts
* **12-bit Down Counter Timer**:
  * High 4 bits: `$FF94` (bits 3–0).
  * Low 8 bits: `$FF95` (bits 7–0).
  * Clock Source (`$FF91` bit 5): $0 = 63.695\ \mu\text{s}$ (60 Hz horizontal line clock / 15.7 kHz), $1 = 279.365\ \text{ns}$ (master oscillator / 3.579545 MHz).
* **Interrupt Routing**:
  * Dedicated GIME IRQ Enable/Status register at `$FF92`.
  * Dedicated GIME FIRQ Enable/Status register at `$FF93`.
  * Sources: Timer, Horizontal Border (HBORD), Vertical Border (VBORD), Serial/RS-232, Keyboard, Cartridge.

### 3.3 CoCo 3 Memory & Keyboard Upgrades
* **RAM**: 128KB standard (expandable to 512KB internally, or up to 2MB with modern upgrades).
* **ROM**: 32KB Super Extended Color BASIC (occupies physical blocks `$3C`–`$3F` or external cart blocks).
* **Keyboard**: 57 keys (adds CTRL, ALT, F1, F2). The additional keys are scanned into the matrix via extra diodes/connections monitored by PIA0 and GIME.

---

## 4. Comprehensive I/O Port & Register Comparison

The following table details every hardware I/O address in the $FF00–$FFFF range, contrasting CoCo 2 and CoCo 3 behavior:

| Address | CoCo 2 Function | CoCo 3 Function | Centipede Emulation Role |
| :--- | :--- | :--- | :--- |
| **`$FF00`** | PIA 0 Port A Data / DDR (Keyboard columns, Joy comparator) | PIA 0 Port A Data / DDR | Transparent pass-through to CoCo 2 PIA 0 |
| **`$FF01`** | PIA 0 Port A Control (HS / IRQ enable) | PIA 0 Port A Control | Transparent pass-through / snoop |
| **`$FF02`** | PIA 0 Port B Data / DDR (Keyboard row strobes) | PIA 0 Port B Data / DDR | Transparent pass-through / inject keypresses |
| **`$FF03`** | PIA 0 Port B Control (FS / IRQ enable) | PIA 0 Port B Control | Transparent pass-through / snoop |
| **`$FF20`** | PIA 1 Port A Data / DDR (DAC 6-bit audio, RS-232) | PIA 1 Port A Data / DDR | Transparent pass-through to CoCo 2 PIA 1 |
| **`$FF21`** | PIA 1 Port A Control (Cassette motor relay) | PIA 1 Port A Control | Transparent pass-through |
| **`$FF22`** | PIA 1 Port B Data / DDR (VDG control lines: GM0-GM2, CSS, A/G) | PIA 1 Port B Data (Lines ignored or shadowed) | Snoop mode bits |
| **`$FF23`** | PIA 1 Port B Control (Cartridge interrupt / Sound enable) | PIA 1 Port B Control | Intercept / assert interrupts |
| **`$FF40`–`$FF48`** | External Floppy Controller (WD1793/WD2793 or WD1773) | Same (WD1773 disk controller) | Emulated by Centipede VFS/floppy coroutine |
| **`$FF7F`** | Multi-Pak Interface (MPI) Slot Select | Multi-Pak Interface (MPI) Slot Select | Supported / intercepted |
| **`$FF90`** | *Unused / Float* | **GIME Init 0**: Mode, MMU enable, ROM mode | **Emulated in Centipede** (tracks MMU state) |
| **`$FF91`** | *Unused / Float* | **GIME Init 1**: Timer clock, Task 0/1 select | **Emulated in Centipede** (switches task map) |
| **`$FF92`** | *Unused / Float* | **GIME IRQ Enable / Status** | **Emulated in Centipede** (triggers CPU IRQ) |
| **`$FF93`** | *Unused / Float* | **GIME FIRQ Enable / Status** | **Emulated in Centipede** (triggers CPU FIRQ) |
| **`$FF94`** | *Unused / Float* | **GIME Timer Count MSB** (4 bits) | **Emulated in Centipede** (down-counter) |
| **`$FF95`** | *Unused / Float* | **GIME Timer Count LSB** (8 bits) | **Emulated in Centipede** (down-counter) |
| **`$FF98`** | *Unused / Float* | **GIME Video Mode**: Text/Graphics, lines | **Emulated in Centipede** (virtual display) |
| **`$FF99`** | *Unused / Float* | **GIME Video Resolution**: Width, BPP | **Emulated in Centipede** (virtual display) |
| **`$FF9A`** | *Unused / Float* | **GIME Border Color** | **Emulated in Centipede** |
| **`$FF9B`** | *Unused / Float* | **GIME 512K RAM Bank Select** | **Emulated in Centipede** |
| **`$FF9C`** | *Unused / Float* | **GIME Vertical Text Scroll** | **Emulated in Centipede** |
| **`$FF9D`** | *Unused / Float* | **GIME Video Offset MSB** (RAM addr) | **Emulated in Centipede** (virtual framebuffer) |
| **`$FF9E`** | *Unused / Float* | **GIME Video Offset LSB** (RAM addr) | **Emulated in Centipede** (virtual framebuffer) |
| **`$FF9F`** | *Unused / Float* | **GIME Horizontal Offset** | **Emulated in Centipede** |
| **`$FFA0`–`$FFA7`** | *Unused / Float* | **GIME MMU Task 0 Bank Regs** (Slots 0–7) | **Emulated in Centipede** (8KB page mapping) |
| **`$FFA8`–`$FFAF`** | *Unused / Float* | **GIME MMU Task 1 Bank Regs** (Slots 0–7) | **Emulated in Centipede** (8KB page mapping) |
| **`$FFB0`–`$FFBF`** | *Unused / Float* | **GIME 16 Palette Registers** | **Emulated in Centipede** (color translation) |
| **`$FFC0`–`$FFC5`** | SAM VDG Mode Selection ($V_0, V_1, V_2$) | Emulated in CoCo 1/2 mode ($FF90.7=0$) | Handled / snooped |
| **`$FFC6`–`$FFD3`** | SAM Display Offset ($F_0$–$F_6$) | Emulated in CoCo 1/2 mode ($FF90.7=0$) | Handled / snooped |
| **`$FFD4`–`$FFD5`** | SAM Page Select ($P_1$ bit clear/set) | Handled by GIME / MMU | Emulated |
| **`$FFD6`–`$FFD9`** | SAM CPU Speed Selection ($R_0, R_1$) | GIME Speed Control (0.895 / 1.78 MHz) | Handled / throttled |
| **`$FFDA`–`$FFDD`** | SAM Memory Size ($M_0, M_1$) | Ignored / Handled by GIME | Emulated |
| **`$FFDE`–`$FFDF`** | SAM Map Type ($TY$ clear/set: 32K vs 64K) | Handled by GIME / MMU | Emulated |
| **`$FFF0`–`$FFFF`** | Reset, Interrupt, and Vector Table | GIME Vector Handling & Constant RAM | Intercepted via $\overline{\text{SLENB}}$ |

---

## 5. Detailed Register Bit Specifications (CoCo 3 / GIME)

### 5.1 GIME Initialization Register 0 (`$FF90`)
```
Bit 7: CoCo 1/2 Compatibility Mode
       0 = CoCo 1/2 mode (SAM VDG registers enabled, legacy MMU behavior)
       1 = CoCo 3 native mode
Bit 6: MMU Enable
       0 = MMU disabled (standard linear map)
       1 = MMU enabled (Task 0 / Task 1 active)
Bit 5: GIME Chip IRQ Master Enable (1 = enabled, 0 = disabled)
Bit 4: GIME Chip FIRQ Master Enable (1 = enabled, 0 = disabled)
Bit 3: Constant RAM at $FE00-$FEFF Enable
       0 = Swapped with MMU bank at $E000-$FFFF
       1 = Fixed to physical RAM page $3F (vital for NitrOS-9 Level 2)
Bit 2: External SCS Enable
       0 = Standard I/O decoding
       1 = External device select for $FF40-$FF5F
Bits 1-0: ROM Mode Select (MC1, MC0)
       0 0 = 16K Internal ROM ($8000-$BFFF), 16K External ROM ($C000-$FEFF)
       0 1 = 16K Internal ROM ($8000-$BFFF), 16K Internal ROM ($C000-$FEFF) (32K Internal)
       1 0 = 32K External ROM
       1 1 = Reserved / All RAM
```

### 5.2 GIME Initialization Register 1 (`$FF91`)
```
Bit 7:   Unused
Bit 6:   DRAM Chip Type (0 = 64K chips, 1 = 256K chips / 512KB total)
Bit 5:   Timer Clock Input Frequency
         0 = 63.695 µs (60 Hz horizontal line tick, ~15.7 kHz)
         1 = 279.365 ns (Master oscillator / 3.579545 MHz)
Bits 4-1: Unused
Bit 0:   MMU Task Select
         0 = Task 0 active (maps using $FFA0-$FFA7)
         1 = Task 1 active (maps using $FFA8-$FFAF)
```

### 5.3 GIME Interrupt Registers (`$FF92` IRQ / `$FF93` FIRQ)
Both registers have identical bit definitions. Writing to the register configures the interrupt mask; reading returns pending interrupt flags:
```
Bits 7-6: Unused
Bit 5:    Timer Interrupt
Bit 4:    Horizontal Border / Blanking Interrupt (HBORD)
Bit 3:    Vertical Border / Blanking Interrupt (VBORD)
Bit 2:    1.79 MHz Serial (RS-232 / ACIA) Interrupt
Bit 1:    Keyboard Keypress Interrupt
Bit 0:    Cartridge Port Interrupt (CART*)
```

### 5.4 GIME MMU Bank Registers (`$FFA0`–`$FFAF`)
Each bank register defines which 8KB physical RAM page is mapped to a 64KB CPU address slot:
```
Logical Slot      Task 0 Register    Task 1 Register    CPU Logical Range
Slot 0                $FFA0              $FFA8          $0000 - $1FFF
Slot 1                $FFA1              $FFA9          $2000 - $3FFF
Slot 2                $FFA2              $FFAA          $4000 - $5FFF
Slot 3                $FFA3              $FFAB          $6000 - $7FFF
Slot 4                $FFA4              $FFAC          $8000 - $9FFF
Slot 5                $FFA5              $FFAD          $A000 - $BFFF
Slot 6                $FFA6              $FFAE          $C000 - $DFFF
Slot 7                $FFA7              $FFAF          $E000 - $FFFF
```
* **Page Value**: Bits 5–0 specify physical 8KB block number (`$00` through `$3F` for 512KB).

---

## 6. Key Differences Matrix: CoCo 2 vs. CoCo 3

| Feature | Tandy Color Computer 2 | Tandy Color Computer 3 | CoCo 2 + Centipede Strategy |
| :--- | :--- | :--- | :--- |
| **Core Chipset** | MC6883 (SAM) + MC6847 (VDG) + 74LS Glue | GIME ASIC (SC81471 / SC84447) | Centipede RP2350B emulates GIME MMU, timers, and RAM over bus |
| **Max Motherboard RAM** | 64KB | 512KB (or 2MB upgraded) | Centipede supplies 512KB physical RAM via SRAM/flash |
| **Memory Architecture**| Linear 64KB with $P_1$ 32K bank swap | 8-slot 8KB MMU with Dual Task Maps (Task 0 & 1) | Centipede translates 6809 addresses in real time via PIO |
| **Kernel Constant RAM**| None | Constant RAM at $FE00–$FEFF mapped to Block $3F | Intercepted and routed to Centipede Block $3F |
| **CPU Speeds** | 0.895 MHz (1.79 MHz blanks screen) | 0.895 MHz / 1.78 MHz without snow | Can throttle or run fast with external video |
| **Text Modes** | 32 × 16, uppercase (lowercase on 6847T1) | 32/40/64/80 columns × 24/25/28 rows | Framebuffer emulated in Centipede; streamed to display |
| **Attribute Byte** | None | Blink, Underline, 8 Foreground, 8 Background | Emulated in virtual framebuffer |
| **Max Graphics Mode** | 256 × 192, 2 colors (PMODE 4) | 640 × 200, 4 colors / 320 × 200, 16 colors | Emulated in virtual framebuffer |
| **Master Color Palette**| Fixed 8 colors | 64 colors (composite) / 64 colors (RGB) | 16 registers ($FFB0-$FFBF) tracked in firmware |
| **Video Outputs** | Composite NTSC and RF TV | Composite NTSC and 10-pin Analog RGB | Streamed over USB / tether console or HDMI/LCD |
| **Programmable Timer** | None (software delay loops only) | 12-bit programmable timer with IRQ/FIRQ | RP2350 hardware timer asserts interrupt lines |
| **OS-9 Support** | NitrOS-9 Level 1 (single-task, 64K) | NitrOS-9 Level 2 (full multi-process MMU) | Enables full NitrOS-9 Level 2 booting on CoCo 2 |
| **Keyboard** | 53 keys | 57 keys (adds CTRL, ALT, F1, F2) | Centipede USB keyboard injection |

---

## 7. How Centipede Enhances CoCo 2 to Emulate CoCo 3

### 7.1 Bus Interception via $\overline{\text{SLENB}}$
On the CoCo cartridge port, pin 32 connects to $\overline{\text{SLENB}}$ (Single Line Enable). When Centipede detects a memory read cycle intended for an emulated CoCo 3 resource (e.g., expanded RAM, CoCo 3 ROMs, or GIME registers):
1. The RP2350B PIO state machine (`gerbil.pio`) detects the falling edge of clock $Q$ and samples $R/\overline{W}$ and Address pins $A_0$–$A_{15}$.
2. If Centipede owns the cycle, it pulls the data byte from its internal lookup table.
3. Centipede asserts $\overline{\text{SLENB}}$ active low and drives the data byte onto $D_0$–$D_7$.
4. Inside the CoCo 2, $\overline{\text{SLENB}}$ disables the motherboard data bus drivers, allowing the CPU to read Centipede's data instead of the motherboard RAM or ROM.
5. On the rising edge of clock $E$, Centipede releases $\overline{\text{SLENB}}$ and returns $D_0$–$D_7$ to high-impedance inputs.

### 7.2 Emulating the GIME MMU & 512KB Physical RAM
Centipede implements the CoCo 3 MMU logic in firmware:
* **State Variables**:
  * `MmuEnabled` (bool, mirrors `$FF90` bit 6).
  * `ConstantRamFeEnabled` (bool, mirrors `$FF90` bit 3).
  * `CurrentTask` (0 or 1, mirrors `$FF91` bit 0).
  * `MmuMap[2][8]` (byte array holding the 8 physical block numbers for Task 0 and Task 1).
* **Address Translation**:
  During each CPU cycle, address $A$ is mapped:
  ```cpp
  inline uint32_t TranslateAddress(uint16_t logical_addr) {
      if (!MmuEnabled) {
          return logical_addr; // Standard 64K linear mode
      }
      // Handle Constant RAM at $FE00-$FFFF
      if (ConstantRamFeEnabled && logical_addr >= 0xFE00) {
          uint32_t physical_block = 0x3F; // Block 63
          return (physical_block << 13) | (logical_addr & 0x1FFF);
      }
      uint slot = (logical_addr >> 13) & 7; // Top 3 bits select slot 0-7
      uint32_t physical_block = MmuMap[CurrentTask][slot] & 0x3F;
      return (physical_block << 13) | (logical_addr & 0x1FFF);
  }
  ```

### 7.3 CoCo 3 ROM Mapping
A stock CoCo 2 has 16KB of BASIC ROM on its motherboard ($8000–$BFFF and $C000–$FEFF). A CoCo 3 requires 32KB of Super Extended BASIC (`coco3.rom`).
* Centipede intercepts accesses to `$8000`–`$FEFF` whenever CoCo 3 ROM mode is selected or when the MMU maps ROM blocks.
* Centipede supplies bytes directly from its internal copy of the CoCo 3 ROMs using $\overline{\text{SLENB}}$, effectively bypassing the CoCo 2's physical ROMs.

### 7.4 Handling GIME Interrupts & Timers
* The CoCo 3 GIME timer counts down at 15.7 kHz or 3.58 MHz.
* An RP2350 hardware timer or background coroutine decrements the 12-bit counter.
* When the counter hits zero and the timer interrupt is unmasked in `$FF92` (IRQ) or `$FF93` (FIRQ), Centipede asserts the cartridge port interrupt pin (`CART*` or `NMI*`) to interrupt the 6809E CPU.

### 7.5 Solving the Video Display Challenge
The only hardware feature of a real CoCo 3 that cannot be routed back through the CoCo 2's RF/composite modulator is the GIME's 80-column RGB video signal (the CoCo 2 motherboard video path is hardwired to the MC6847 VDG chip).
Centipede handles this through **dual-mode video**:
1. **CoCo 2 Compatible Screens (32×16 Text, PMODEs)**: Centipede writes the display data into motherboard RAM so the native CoCo 2 MC6847 can still generate video on a physical TV or monitor.
2. **CoCo 3 Native Screens (40/80-column Text, 320/640-pixel Graphics)**:
   * Centipede tracks video offset registers (`$FF9D`–`$FF9E`), video modes (`$FF98`–`$FF99`), and palette registers (`$FFB0`–`$FFBF`).
   * Centipede's background core snoops video RAM writes and renders a virtual CoCo 3 frame buffer.
   * The rendered display is output via the high-speed USB tether to a PC screen, a terminal window, or a dedicated Centiscope/DVI display peripheral.

---

## 8. Implementation Checklist for CoCo 3 Mode on Centipede

- [ ] **Activate GIME Register Range**: Route writes to `$FF90`–`$FFBF` into GIME handler routines rather than ignoring them or forwarding to the CoCo 2 bus.
- [ ] **Integrate `DoCoco3Mmu`**: Enable 8-block address translation on read/write cycles using `MmuMap[2][8]`.
- [ ] **Constant RAM Support**: Force `$FE00`–`$FEFF` to physical page `$3F` when `$FF90` bit 3 is asserted.
- [ ] **12-bit Timer Emulation**: Implement down-counter decrement and `$FF92`/`$FF93` interrupt triggering.
- [ ] **Load 32KB CoCo 3 Super Extended BASIC**: Serve `coco3.0x8000.rom` across `$8000`–`$FEFF` via $\overline{\text{SLENB}}$.
- [ ] **Virtual Video Snooping**: Monitor video offset and mode registers to render 80-column text and 640×200 graphics for tethered viewing.
- [ ] **Verify NitrOS-9 Level 2 Boot**: Boot a standard CoCo 3 NitrOS-9 Level 2 disk image over Centipede VFS.
