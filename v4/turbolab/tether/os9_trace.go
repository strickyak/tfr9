package main

import (
	"fmt"
	"io"
	"strings"

	"github.com/strickyak/tfr9/v4/turbolab/tether/os9"
)

type PendingSwi2 struct {
	Serial uint
	Label  string // "_%d_"
	PC     uint16 // Address of the SWI2 instruction ($10 $3F)
	Cycle  uint64
	Os9Num byte
	Call   *os9.Os9ApiCall
	Stack  uint16 // Stack pointer S where CC was written
}

type Regs struct {
	CC byte
	A  byte
	B  byte
	DP byte
	X  uint16
	Y  uint16
	U  uint16
	PC uint16
	D  uint16
}

type Os9Tracer struct {
	Out     io.Writer
	Serial  uint
	Pending map[uint]*PendingSwi2 // key: (uint(stack) << 16) | uint(swi2_pc)

	// SWI2 detection state
	sawPrefix10    bool
	prefix10Addr   uint16
	prefix10Cycle  uint64
	swi2State      int // 0: idle, 1: saw 10 3F (wait os9num), 2: waiting for 12 writes
	swi2PC         uint16
	swi2Cycle      uint64
	swi2Num        byte
	swi2WriteCount int
	swi2WriteStack uint16
	swi2WriteBytes [12]byte // 11 down to 0: PC.lo, PC.hi, U.lo, U.hi, Y.lo, Y.hi, X.lo, X.hi, DP, B, A, CC

	// RTI detection state
	rtiState         int // 0: idle, 1: saw 3B (wait CC read), 2: reading rest of stack
	rtiPC            uint16
	rtiCycle         uint64
	rtiStack         uint16
	rtiReadCount     int
	rtiExpectedReads int
	rtiReadBytes     [12]byte // 0 to 11: CC, A, B, DP, X.hi, X.lo, Y.hi, Y.lo, U.hi, U.lo, PC.hi, PC.lo
}

func NewOs9Tracer(out io.Writer) *Os9Tracer {
	return &Os9Tracer{
		Out:     out,
		Pending: make(map[uint]*PendingSwi2),
	}
}

func FormatCall(os9num byte, call *os9.Os9ApiCall, regs *Regs) string {
	var buf strings.Builder
	if call == nil {
		fmt.Fprintf(&buf, "$%02X = UNKNOWN ( D=$%04X, X=$%04X, Y=$%04X, U=$%04X )", os9num, regs.D, regs.X, regs.Y, regs.U)
	} else {
		fmt.Fprintf(&buf, "$%02X = %s ( ", os9num, call.Name)
		hasArg := false
		if call.A != "" {
			fmt.Fprintf(&buf, "A=%s=$%02X, ", call.A, regs.A)
			hasArg = true
		}
		if call.B != "" {
			if call.B == "errnum" {
				errname := os9.Os9ApiErrorNames[regs.B]
				if errname != "" {
					fmt.Fprintf(&buf, "B=%s=$%02X=%s, ", call.B, regs.B, errname)
				} else {
					fmt.Fprintf(&buf, "B=%s=$%02X, ", call.B, regs.B)
				}
			} else {
				fmt.Fprintf(&buf, "B=%s=$%02X, ", call.B, regs.B)
			}
			hasArg = true
		}
		if call.D != "" {
			fmt.Fprintf(&buf, "D=%s=$%04X, ", call.D, regs.D)
			hasArg = true
		}
		if call.X != "" {
			fmt.Fprintf(&buf, "X=%s=$%04X, ", call.X, regs.X)
			hasArg = true
		}
		if call.Y != "" {
			fmt.Fprintf(&buf, "Y=%s=$%04X, ", call.Y, regs.Y)
			hasArg = true
		}
		if call.U != "" {
			fmt.Fprintf(&buf, "U=%s=$%04X, ", call.U, regs.U)
			hasArg = true
		}
		if !hasArg {
			buf.WriteString(")")
		} else {
			s := buf.String()
			if strings.HasSuffix(s, ", ") {
				return s[:len(s)-2] + " )"
			}
			buf.WriteString(")")
		}
	}
	return buf.String()
}

func FormatReturn(call *os9.Os9ApiCall, regs *Regs) string {
	if (regs.CC & 1) != 0 {
		errname := os9.Os9ApiErrorNames[regs.B]
		errdesc := strings.TrimSpace(os9.Os9ApiErrorDescription[regs.B])
		if errname != "" {
			if errdesc != "" {
				return fmt.Sprintf("ERROR $%02X (%d.) %s: %s", regs.B, regs.B, errname, errdesc)
			}
			return fmt.Sprintf("ERROR $%02X (%d.) %s", regs.B, regs.B, errname)
		}
		return fmt.Sprintf("ERROR $%02X (%d.)", regs.B, regs.B)
	}

	var buf strings.Builder
	buf.WriteString("( ")
	hasRet := false
	if call != nil {
		if call.RA != "" {
			fmt.Fprintf(&buf, "RA=%s=$%02X, ", call.RA, regs.A)
			hasRet = true
		}
		if call.RB != "" {
			fmt.Fprintf(&buf, "RB=%s=$%02X, ", call.RB, regs.B)
			hasRet = true
		}
		if call.RD != "" {
			fmt.Fprintf(&buf, "RD=%s=$%04X, ", call.RD, regs.D)
			hasRet = true
		}
		if call.RX != "" {
			fmt.Fprintf(&buf, "RX=%s=$%04X, ", call.RX, regs.X)
			hasRet = true
		}
		if call.RY != "" {
			fmt.Fprintf(&buf, "RY=%s=$%04X, ", call.RY, regs.Y)
			hasRet = true
		}
		if call.RU != "" {
			fmt.Fprintf(&buf, "RU=%s=$%04X, ", call.RU, regs.U)
			hasRet = true
		}
	}
	if !hasRet {
		if call == nil {
			fmt.Fprintf(&buf, "RD=$%04X, RX=$%04X, RY=$%04X, RU=$%04X, ", regs.D, regs.X, regs.Y, regs.U)
		} else {
			buf.WriteString("ok")
		}
	}
	s := buf.String()
	if strings.HasSuffix(s, ", ") {
		return s[:len(s)-2] + " )"
	}
	return s + " )"
}

func (t *Os9Tracer) OnCycle(rawKind byte, addr uint16, data byte, cycle uint64) {
	kind := rawKind & 0x0F
	flags := rawKind & 0xF0

	// ── SWI2 State Machine ──
	if (kind == KIND_FIC || (flags&FLAG_LIC) != 0) && data == 0x10 && addr < 0xFFF0 {
		t.sawPrefix10 = true
		t.prefix10Addr = addr
		t.prefix10Cycle = cycle
	} else if t.sawPrefix10 {
		t.sawPrefix10 = false
		if data == 0x3F && addr == t.prefix10Addr+1 {
			t.swi2PC = t.prefix10Addr
			t.swi2Cycle = t.prefix10Cycle
			t.swi2State = 1 // wait for os9num read
		}
	} else if t.swi2State == 1 {
		if kind != KIND_WRITE && kind != KIND_IDLE {
			t.swi2Num = data
			t.swi2State = 2 // wait for 12 writes
			t.swi2WriteCount = 0
		}
	} else if t.swi2State == 2 {
		if kind == KIND_WRITE {
			t.swi2WriteBytes[11-t.swi2WriteCount] = data
			t.swi2WriteStack = addr
			t.swi2WriteCount++
			if t.swi2WriteCount == 12 {
				t.swi2State = 0
				t.handleSwi2Call()
			}
		} else if kind == KIND_FIC {
			// Sequence aborted by new instruction
			t.swi2State = 0
		}
	}

	// ── RTI State Machine ──
	if (kind == KIND_FIC || (flags&FLAG_LIC) != 0) && data == 0x3B && addr < 0xFFF0 {
		t.rtiPC = addr
		t.rtiCycle = cycle
		t.rtiState = 1 // wait for CC read
		t.rtiReadCount = 0
	} else if t.rtiState == 1 {
		if kind != KIND_WRITE && kind != KIND_IDLE {
			if addr == t.rtiPC+1 {
				// Intermediate read cycle on 6809, ignore
			} else {
				// CC read from stack
				t.rtiStack = addr
				t.rtiReadBytes[0] = data
				t.rtiReadCount = 1
				if (data & 0x80) != 0 {
					t.rtiExpectedReads = 12 // Entire frame
				} else {
					t.rtiExpectedReads = 3 // FIRQ (CC, PC.hi, PC.lo)
				}
				t.rtiState = 2
			}
		}
	} else if t.rtiState == 2 {
		if kind != KIND_WRITE && kind != KIND_IDLE {
			t.rtiReadBytes[t.rtiReadCount] = data
			t.rtiReadCount++
			if t.rtiReadCount == t.rtiExpectedReads {
				t.rtiState = 0
				t.handleRti()
			}
		} else if kind == KIND_FIC {
			// Sequence aborted
			t.rtiState = 0
		}
	}
}

func (t *Os9Tracer) handleSwi2Call() {
	cc := t.swi2WriteBytes[0]
	a := t.swi2WriteBytes[1]
	b := t.swi2WriteBytes[2]
	dp := t.swi2WriteBytes[3]
	x := (uint16(t.swi2WriteBytes[4]) << 8) | uint16(t.swi2WriteBytes[5])
	y := (uint16(t.swi2WriteBytes[6]) << 8) | uint16(t.swi2WriteBytes[7])
	u := (uint16(t.swi2WriteBytes[8]) << 8) | uint16(t.swi2WriteBytes[9])
	pc := (uint16(t.swi2WriteBytes[10]) << 8) | uint16(t.swi2WriteBytes[11])
	regs := &Regs{
		CC: cc,
		A:  a,
		B:  b,
		DP: dp,
		X:  x,
		Y:  y,
		U:  u,
		PC: pc,
		D:  (uint16(a) << 8) | uint16(b),
	}

	t.Serial++
	label := fmt.Sprintf("_%d_", t.Serial)

	call := os9.Os9ApiCallOf[t.swi2Num]
	callDesc := FormatCall(t.swi2Num, call, regs)

	if t.swi2Num == 0x06 || t.swi2Num == 0x05 {
		// F$Exit ($06) and F$Chain ($05) never return via RTI
		fmt.Fprintf(t.Out, "i SWI2 %s #%d at $%04X: %s (never returns)\n", label, t.swi2Cycle, t.swi2PC, callDesc)
	} else {
		fmt.Fprintf(t.Out, "i SWI2 %s #%d at $%04X: %s\n", label, t.swi2Cycle, t.swi2PC, callDesc)
		key := (uint(t.swi2WriteStack) << 16) | uint(t.swi2PC)
		t.Pending[key] = &PendingSwi2{
			Serial: t.Serial,
			Label:  label,
			PC:     t.swi2PC,
			Cycle:  t.swi2Cycle,
			Os9Num: t.swi2Num,
			Call:   call,
			Stack:  t.swi2WriteStack,
		}
	}
}

func (t *Os9Tracer) handleRti() {
	if t.rtiExpectedReads == 12 {
		cc := t.rtiReadBytes[0]
		a := t.rtiReadBytes[1]
		b := t.rtiReadBytes[2]
		dp := t.rtiReadBytes[3]
		x := (uint16(t.rtiReadBytes[4]) << 8) | uint16(t.rtiReadBytes[5])
		y := (uint16(t.rtiReadBytes[6]) << 8) | uint16(t.rtiReadBytes[7])
		u := (uint16(t.rtiReadBytes[8]) << 8) | uint16(t.rtiReadBytes[9])
		pc := (uint16(t.rtiReadBytes[10]) << 8) | uint16(t.rtiReadBytes[11])
		regs := &Regs{
			CC: cc,
			A:  a,
			B:  b,
			DP: dp,
			X:  x,
			Y:  y,
			U:  u,
			PC: pc,
			D:  (uint16(a) << 8) | uint16(b),
		}

		swi2PC := pc - 3
		key := (uint(t.rtiStack) << 16) | uint(swi2PC)
		pending, ok := t.Pending[key]
		if !ok {
			// Fallback: search by stack in case return PC had slight offset
			for k, p := range t.Pending {
				if p.Stack == t.rtiStack {
					pending = p
					ok = true
					delete(t.Pending, k)
					break
				}
			}
		} else {
			delete(t.Pending, key)
		}

		if ok {
			callName := "UNKNOWN"
			if pending.Call != nil {
				callName = pending.Call.Name
			}
			retDesc := FormatReturn(pending.Call, regs)
			fmt.Fprintf(t.Out, "i RTI  %s #%d returning to SWI2 $%02X (%s): %s\n",
				pending.Label, t.rtiCycle, pending.Os9Num, callName, retDesc)
		} else {
			// RTI returning from hardware interrupt (e.g. IRQ)
			fmt.Fprintf(t.Out, "i RTI      #%d returning from interrupt to $%04X\n", t.rtiCycle, pc)
		}
	} else if t.rtiExpectedReads == 3 {
		// FIRQ return (CC, PC.hi, PC.lo)
		pc := (uint16(t.rtiReadBytes[1]) << 8) | uint16(t.rtiReadBytes[2])
		fmt.Fprintf(t.Out, "i RTI      #%d returning from FIRQ to $%04X\n", t.rtiCycle, pc)
	}
}
