# CoCo 2 to CoCo 3 Emulation: Architectural Decisions Log

This document records the architectural and implementation decisions for enhancing a **Tandy Color Computer 2** with the **Centipede Board** (RP2350B) to emulate a **Color Computer 3** on branch `work-sept25-try-coco3`.

---

## Decision 1: Fixed CPU Clock Speed (No Dynamic Overclocking)

* **Date**: 2026-09-25
* **Status**: Accepted
* **Context**:
  On a real CoCo 3, the GIME ASIC generates interleaved memory cycles, allowing the 6809E CPU to safely run at 1.78 MHz without video snow or display blanking. On a stock CoCo 2, however, the 6809E clocks ($E$ and $Q$) are generated on the motherboard by the MC6883 SAM. While the SAM supports a high-speed mode (selected via writes to `$FFD9`), running at 1.79 MHz on a CoCo 2 causes memory access collisions with the MC6847 VDG and blanks or garbles the physical display.
* **Decision**:
  1. We will **not** attempt to change the CPU clock speed; the 6809E will remain at its nominal CoCo 2 clock speed (~**0.895 MHz**).
  2. Writes to SAM/GIME speed registers (`$FFD6`–`$FFD9`) will be acknowledged and tracked in state if required by software, but will not attempt to accelerate or overdrive the host 6809 bus.

---

## Decision 2: Boot in VDG 32×16 Compatibility Mode; Target 320×200 at 16 Colors for CoCo 3 Mode

* **Date**: 2026-09-25
* **Status**: Accepted
* **Context**:
  The physical video output jacks (RF and composite) of the host CoCo 2 motherboard are driven directly by the Motorola MC6847 VDG. The MC6847 natively supports only 32×16 uppercase text and PMODE 0–4 graphics (maximum 256×192 at 2 colors). It cannot generate CoCo 3 40/80-column text or 16-color graphics on the physical TV jack.
* **Decision**:
  1. **Boot Mode**:
     * The system will boot into **CoCo 1/2 VDG Compatibility Mode** (32 columns × 16 rows text).
     * The initialization register `$FF90` bit 7 will default to `0` (CoCo 1/2 compatibility mode).
     * Motherboard video RAM at `$0400`–`$05FF` will be populated so the physical CoCo 2 video output works out-of-the-box on a standard monitor or TV.
  2. **CoCo 3 Graphics Target**:
     * Our primary target for demonstrating successful CoCo 3 operation will be switching to the hallmark CoCo 3 graphics mode:
       **320 × 200 at 16 colors** (32,000 bytes per screen buffer, 4 bits per pixel, 16 programmable palette registers from `$FFB0`–`$FFBF`).
     * When software configures the GIME registers for this mode (`$FF90.7 = 1`, `$FF98` graphics mode enabled, `$FF99` configured for 320 horizontal resolution and 16 colors, and video offset registers `$FF9D`–`$FF9E` set):
       - Centipede will maintain the 32KB graphics buffer in its physical RAM emulation.
       - Centipede will snoop/buffer the 16 palette registers and display RAM updates.
       - Centipede will render or stream the resulting 320×200 16-color image to the tethered PC console, virtual display, or viewer (e.g., Centiscope / USB pipeline).

---

## Future Decisions Log

*(Append new decisions below as project development continues.)*
