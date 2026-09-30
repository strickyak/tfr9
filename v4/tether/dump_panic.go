package main

import (
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/strickyak/tfr9/v4/tether/lib"
)

// formatCC formats the 6809/6309 Condition Code register flags.
// Bit 7: E (Entire)
// Bit 6: F (FIRQ mask)
// Bit 5: H (Half carry)
// Bit 4: I (IRQ mask)
// Bit 3: N (Negative)
// Bit 2: Z (Zero)
// Bit 1: V (Overflow)
// Bit 0: C (Carry)
func formatCC(cc byte) string {
	chars := []byte("efhinzvc")
	if (cc & 0x80) != 0 {
		chars[0] = 'E'
	}
	if (cc & 0x40) != 0 {
		chars[1] = 'F'
	}
	if (cc & 0x20) != 0 {
		chars[2] = 'H'
	}
	if (cc & 0x10) != 0 {
		chars[3] = 'I'
	}
	if (cc & 0x08) != 0 {
		chars[4] = 'N'
	}
	if (cc & 0x04) != 0 {
		chars[5] = 'Z'
	}
	if (cc & 0x02) != 0 {
		chars[6] = 'V'
	}
	if (cc & 0x01) != 0 {
		chars[7] = 'C'
	}
	return string(chars)
}

// dumpHexLines formats 16-byte memory rows, skipping runs of all-zero lines.
func dumpHexLines(w io.Writer, ram []byte, start uint, end uint) {
	if ram == nil || int(start) >= len(ram) {
		return
	}
	if int(end) > len(ram) {
		end = uint(len(ram))
	}
	startAligned := start & ^uint(15)
	endAligned := (end + 15) & ^uint(15)
	if int(endAligned) > len(ram) {
		endAligned = uint(len(ram))
	}

	skipping := false
	for addr := startAligned; addr < endAligned; addr += 16 {
		allZero := true
		for j := uint(0); j < 16 && int(addr+j) < len(ram); j++ {
			if ram[addr+j] != 0 {
				allZero = false
				break
			}
		}

		if allZero && addr > startAligned && addr+16 < endAligned {
			if !skipping {
				fmt.Fprintln(w, "  *")
				skipping = true
			}
			continue
		}
		skipping = false

		var hexPart strings.Builder
		var asciiPart strings.Builder
		for j := uint(0); j < 16; j++ {
			if j == 8 {
				hexPart.WriteString(" ")
			}
			if int(addr+j) < len(ram) {
				b := ram[addr+j]
				fmt.Fprintf(&hexPart, "%02X ", b)
				if b >= 32 && b <= 126 {
					asciiPart.WriteByte(b)
				} else {
					asciiPart.WriteByte('.')
				}
			} else {
				hexPart.WriteString("   ")
				asciiPart.WriteByte(' ')
			}
		}
		fmt.Fprintf(w, "  $%04X: %s |%s|\n", addr, hexPart.String(), asciiPart.String())
	}
}

// dumpDisasm disassembles count instructions from startPC using lib.Decode.
func dumpDisasm(w io.Writer, ram Rammer, startPC uint, count int) {
	trackRam := ram.GetTrackRam()
	if trackRam == nil {
		return
	}
	cur := startPC
	for i := 0; i < count && cur < 0xFFF0; i++ {
		marker := "  "
		if cur == startPC {
			marker = "=>"
		}
		dis, nBytes, _, _, ok := lib.Decode(trackRam[cur:])
		if !ok || nBytes <= 0 {
			fmt.Fprintf(w, "  %s $%04X:  %02X             <unknown>\n", marker, cur, trackRam[cur])
			cur++
			continue
		}
		var hexBytes []string
		for b := 0; b < nBytes && int(cur)+b < len(trackRam); b++ {
			hexBytes = append(hexBytes, fmt.Sprintf("%02X", trackRam[int(cur)+b]))
		}
		hexStr := fmt.Sprintf("%-12s", strings.Join(hexBytes, " "))
		fmt.Fprintf(w, "  %s $%04X:  %s %s\n", marker, cur, hexStr, dis)
		cur += uint(nBytes)
	}
}

// Dump6309PanicState performs an in-depth dump of 6309 CPU state, registers,
// process descriptor, stack, vectors, and memory when a PANIC occurs.
func Dump6309PanicState(ram Rammer) {
	out := os.Stdout

	fmt.Fprintln(out, "")
	fmt.Fprintln(out, "######################################################################")
	fmt.Fprintln(out, "#                       TARGET 6309 CPU PANIC                        #")
	fmt.Fprintln(out, "######################################################################")

	if ram == nil {
		fmt.Fprintln(out, "Error: ram is nil, cannot dump CPU state.")
		return
	}

	trackRam := ram.GetTrackRam()
	if trackRam != nil && len(trackRam) >= 65536 {
		dumpPath := "/tmp/fault.img"
		err := os.WriteFile(dumpPath, trackRam[:65536], 0644)
		if err == nil {
			fmt.Fprintf(out, "Core memory dump written to %s (65536 bytes)\n\n", dumpPath)
		} else {
			fmt.Fprintf(out, "Warning: failed to write %s: %v\n\n", dumpPath, err)
		}
	}

	// 1. Hardware / System Vectors ($FFF0-$FFFF)
	fmt.Fprintln(out, "--- 6309 SYSTEM VECTORS ($FFF0-$FFFF) ---")
	fmt.Fprintf(out, "  RESET: $%04X    NMI:  $%04X    SWI:  $%04X    IRQ:  $%04X\n",
		ram.Peek2(0xFFFE), ram.Peek2(0xFFFC), ram.Peek2(0xFFFA), ram.Peek2(0xFFF8))
	fmt.Fprintf(out, "  FIRQ:  $%04X    SWI2: $%04X    SWI3: $%04X    TRAP: $%04X\n",
		ram.Peek2(0xFFF6), ram.Peek2(0xFFF4), ram.Peek2(0xFFF2), ram.Peek2(0xFFF0))

	// 2. NitrOS-9 Direct Page Vectors ($0020-$0048)
	fmt.Fprintln(out, "\n--- NitrOS-9 DIRECT PAGE VECTORS ($0020-$0048) ---")
	fmt.Fprintf(out, "  D.SWI3:   $%04X    D.SWI2:   $%04X    D.FIRQ:   $%04X\n",
		ram.Peek2(L1_D_SWI3), ram.Peek2(L1_D_SWI2), ram.Peek2(L1_D_FIRQ))
	fmt.Fprintf(out, "  D.IRQ:    $%04X    D.SWI:    $%04X    D.NMI:    $%04X\n",
		ram.Peek2(L1_D_IRQ), ram.Peek2(L1_D_SWI), ram.Peek2(L1_D_NMI))
	fmt.Fprintf(out, "  D.SysSvc: $%04X    D.UsrSvc: $%04X    D.SysDis: $%04X    D.UsrDis: $%04X\n",
		ram.Peek2(L1_D_SysSvc), ram.Peek2(L1_D_UsrSvc), ram.Peek2(L1_D_SysDis), ram.Peek2(L1_D_UsrDis))

	// 3. Process Descriptor & CPU Registers
	dProc := ram.Peek2(L1_D_Proc)
	fmt.Fprintln(out, "\n--- CURRENT PROCESS & CPU REGISTERS ---")
	if dProc == 0 || dProc >= 0xFF00 {
		fmt.Fprintf(out, "  D.Proc: $%04X (No active NitrOS-9 process descriptor)\n", dProc)
	} else {
		pid := ram.Peek1(dProc + L1_P_ID)
		ppid := ram.Peek1(dProc + L1_P_PID)
		state := ram.Peek1(dProc + L1_P_State)
		prior := ram.Peek1(dProc + L1_P_Prior)
		pages := ram.Peek1(dProc + L1_P_PagCnt)
		user := ram.Peek2(dProc + L1_P_User)
		pmod := ram.Peek2(dProc + L1_P_PModul)
		sp := ram.Peek2(dProc + L1_P_SP)

		modName := "<unknown>"
		if pmod != 0 && pmod < 0xFF00 && ram.Peek2(pmod) == 0x87CD {
			nameOff := ram.Peek2(pmod + 4)
			var nb []byte
			for i := uint(0); i < 32; i++ {
				b := ram.Peek1(pmod + nameOff + i)
				nb = append(nb, b&0x7F)
				if (b & 0x80) != 0 {
					break
				}
			}
			modName = string(nb)
		}

		fmt.Fprintf(out, "  Process:  D.Proc=$%04X  PID=%d  PPID=%d  User=%d  Prior=%d  Pages=%d ($%04X bytes)\n",
			dProc, pid, ppid, user, prior, pages, uint(pages)*256)
		fmt.Fprintf(out, "  Module:   $%04X %q (State: $%02X)\n", pmod, modName, state)
		fmt.Fprintf(out, "  P$SP:     $%04X\n", sp)

		if sp >= 0x0100 && sp <= 0xFFEE {
			cc := ram.Peek1(sp + L1_R_CC)
			a := ram.Peek1(sp + L1_R_A)
			b := ram.Peek1(sp + L1_R_B)
			d := (uint(a) << 8) | uint(b)
			dp := ram.Peek1(sp + L1_R_DP)
			x := ram.Peek2(sp + L1_R_X)
			y := ram.Peek2(sp + L1_R_Y)
			u := ram.Peek2(sp + L1_R_U)
			pc := ram.Peek2(sp + L1_R_PC)

			fmt.Fprintf(out, "\n  PC: $%04X    S:  $%04X    U: $%04X\n", pc, sp, u)
			fmt.Fprintf(out, "  X:  $%04X    Y:  $%04X    D: $%04X (A=$%02X B=$%02X)\n", x, y, d, a, b)
			fmt.Fprintf(out, "  DP: $%02X      CC: $%02X [%s]\n", dp, cc, formatCC(cc))
			fmt.Fprintf(out, "       Flags: E=%d F=%d H=%d I=%d N=%d Z=%d V=%d C=%d\n",
				(cc>>7)&1, (cc>>6)&1, (cc>>5)&1, (cc>>4)&1,
				(cc>>3)&1, (cc>>2)&1, (cc>>1)&1, cc&1)

			md := ram.Peek1(L1_D_MDREG)
			if md != 0 {
				fmt.Fprintf(out, "  6309 MD: $%02X\n", md)
			}

			// Code disassembly around PC
			if pc > 0 && pc < 0xFFF0 {
				fmt.Fprintln(out, "\n--- CODE DISASSEMBLY AT PC ---")
				dumpDisasm(out, ram, pc, 8)
			}

			// Stack memory dump around SP
			fmt.Fprintf(out, "\n--- STACK MEMORY (S = $%04X) ---\n", sp)
			dumpHexLines(out, trackRam, sp, sp+32)
		}
	}

	// 4. Direct Page ($0000-$00FF)
	fmt.Fprintln(out, "\n--- DIRECT PAGE ($0000-$00FF) ---")
	dumpHexLines(out, trackRam, 0, 0x100)

	// 5. Tether tracking state
	fmt.Fprintln(out, "\n--- TETHER BUS & SYSTEM CALL STATE ---")
	fmt.Fprintf(out, "  Total Bus Cycles: %d\n", Cycle)
	if Swi2PC != 0 {
		fmt.Fprintf(out, "  Last SWI2 Call:   $%04X (Num=$%02X at Cycle %d)\n", Swi2PC, Swi2Num, Swi2Cycle)
	}
	fmt.Fprintln(out, "######################################################################")
}
