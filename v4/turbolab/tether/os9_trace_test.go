package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestOs9TracerSwi2AndRtiSuccess(t *testing.T) {
	var buf bytes.Buffer
	tracer := NewOs9Tracer(&buf)

	// Simulate SWI2 at $0500: 10 3F 84 (I$Open)
	// Registers to push: CC=0x80, A=0x01, B=0x02, DP=0x00, X=0x0400, Y=0x0010, U=0x0500, PC=0x0503
	cy := uint64(100)
	tracer.OnCycle(KIND_FIC, 0x0500, 0x10, cy)
	cy++
	tracer.OnCycle(KIND_OPCODE_CONT, 0x0501, 0x3F, cy)
	cy++
	tracer.OnCycle(KIND_READ, 0x0502, 0x84, cy) // I$Open
	cy++

	// 12 stack writes (S-1 down to S-12): PC.lo, PC.hi, U.lo, U.hi, Y.lo, Y.hi, X.lo, X.hi, DP, B, A, CC
	writes := []struct {
		addr uint16
		data byte
	}{
		{0x0FEE, 0x03}, // PC.lo
		{0x0FED, 0x05}, // PC.hi ($0503)
		{0x0FEC, 0x00}, // U.lo
		{0x0FEB, 0x05}, // U.hi ($0500)
		{0x0FEA, 0x10}, // Y.lo
		{0x0FE9, 0x00}, // Y.hi ($0010)
		{0x0FE8, 0x00}, // X.lo
		{0x0FE7, 0x04}, // X.hi ($0400)
		{0x0FE6, 0x00}, // DP
		{0x0FE5, 0x02}, // B
		{0x0FE4, 0x01}, // A (access_mode = 1)
		{0x0FE3, 0x80}, // CC (E=1, Carry=0)
	}

	for _, w := range writes {
		tracer.OnCycle(KIND_WRITE, w.addr, w.data, cy)
		cy++
	}

	out := buf.String()
	if !strings.Contains(out, "i SWI2 _1_") {
		t.Fatalf("Expected 'i SWI2 _1_', got:\n%s", out)
	}
	if !strings.Contains(out, "$84 = I$Open") {
		t.Fatalf("Expected '$84 = I$Open', got:\n%s", out)
	}
	if !strings.Contains(out, "A=access_mode=$01") || !strings.Contains(out, "X=$pathname=$0400") {
		t.Fatalf("Expected arguments in SWI2 line, got:\n%s", out)
	}

	// Verify pending map has entry
	if len(tracer.Pending) != 1 {
		t.Fatalf("Expected 1 pending call, got %d", len(tracer.Pending))
	}

	// Now simulate RTI returning from I$Open at $0200
	buf.Reset()
	tracer.OnCycle(KIND_FIC, 0x0200, 0x3B, cy) // RTI
	cy++
	tracer.OnCycle(KIND_READ, 0x0201, 0x00, cy) // Intermediate read
	cy++

	// 12 stack reads (S up to S+11): CC, A, B, DP, X.hi, X.lo, Y.hi, Y.lo, U.hi, U.lo, PC.hi, PC.lo
	reads := []struct {
		addr uint16
		data byte
	}{
		{0x0FE3, 0x80}, // CC (Carry=0 -> Success)
		{0x0FE4, 0x03}, // A (new path = 3)
		{0x0FE5, 0x00}, // B
		{0x0FE6, 0x00}, // DP
		{0x0FE7, 0x04}, // X.hi
		{0x0FE8, 0x05}, // X.lo ($0405)
		{0x0FE9, 0x00}, // Y.hi
		{0x0FEA, 0x10}, // Y.lo
		{0x0FEB, 0x05}, // U.hi
		{0x0FEC, 0x00}, // U.lo
		{0x0FED, 0x05}, // PC.hi
		{0x0FEE, 0x03}, // PC.lo ($0503)
	}

	for _, r := range reads {
		tracer.OnCycle(KIND_READ, r.addr, r.data, cy)
		cy++
	}

	out = buf.String()
	if !strings.Contains(out, "i RTI  _1_") {
		t.Fatalf("Expected 'i RTI  _1_', got:\n%s", out)
	}
	if !strings.Contains(out, "returning to SWI2 $84 (I$Open)") {
		t.Fatalf("Expected 'returning to SWI2 $84 (I$Open)', got:\n%s", out)
	}
	if !strings.Contains(out, "RA=path=$03") || !strings.Contains(out, "RX=$after_pathname=$0405") {
		t.Fatalf("Expected result values, got:\n%s", out)
	}

	// Verify pending map is now empty
	if len(tracer.Pending) != 0 {
		t.Fatalf("Expected 0 pending calls after RTI, got %d", len(tracer.Pending))
	}
}

func TestOs9TracerSwi2AndRtiError(t *testing.T) {
	var buf bytes.Buffer
	tracer := NewOs9Tracer(&buf)

	// Simulate SWI2 at $0600: 10 3F 84 (I$Open)
	cy := uint64(500)
	tracer.OnCycle(KIND_FIC, 0x0600, 0x10, cy)
	cy++
	tracer.OnCycle(KIND_OPCODE_CONT, 0x0601, 0x3F, cy)
	cy++
	tracer.OnCycle(KIND_READ, 0x0602, 0x84, cy)
	cy++

	writes := []struct {
		addr uint16
		data byte
	}{
		{0x0FEE, 0x03}, // PC.lo
		{0x0FED, 0x06}, // PC.hi ($0603)
		{0x0FEC, 0x00}, {0x0FEB, 0x00},
		{0x0FEA, 0x00}, {0x0FE9, 0x00},
		{0x0FE8, 0x00}, {0x0FE7, 0x04},
		{0x0FE6, 0x00}, {0x0FE5, 0x00},
		{0x0FE4, 0x01},
		{0x0FE3, 0x80},
	}
	for _, w := range writes {
		tracer.OnCycle(KIND_WRITE, w.addr, w.data, cy)
		cy++
	}

	// Now simulate RTI with Carry set: error $D8 (E$PNNF)
	buf.Reset()
	tracer.OnCycle(KIND_FIC, 0x0200, 0x3B, cy)
	cy++
	tracer.OnCycle(KIND_READ, 0x0201, 0x00, cy)
	cy++

	reads := []struct {
		addr uint16
		data byte
	}{
		{0x0FE3, 0x81}, // CC (Carry=1 -> Error)
		{0x0FE4, 0x00}, // A
		{0x0FE5, 0xD8}, // B = 216 = E$PNNF
		{0x0FE6, 0x00}, {0x0FE7, 0x00}, {0x0FE8, 0x00},
		{0x0FE9, 0x00}, {0x0FEA, 0x00}, {0x0FEB, 0x00},
		{0x0FEC, 0x00},
		{0x0FED, 0x06}, {0x0FEE, 0x03}, // PC ($0603)
	}
	for _, r := range reads {
		tracer.OnCycle(KIND_READ, r.addr, r.data, cy)
		cy++
	}

	out := buf.String()
	if !strings.Contains(out, "i RTI  _1_") {
		t.Fatalf("Expected 'i RTI  _1_', got:\n%s", out)
	}
	if !strings.Contains(out, "ERROR $D8 (216.) E$PNNF: Path Name Not Found") {
		t.Fatalf("Expected formatted error with E$PNNF and description, got:\n%s", out)
	}
}

func TestOs9TracerNonReturningCall(t *testing.T) {
	var buf bytes.Buffer
	tracer := NewOs9Tracer(&buf)

	// Simulate F$Exit ($06)
	cy := uint64(1000)
	tracer.OnCycle(KIND_FIC, 0x0800, 0x10, cy)
	cy++
	tracer.OnCycle(KIND_OPCODE_CONT, 0x0801, 0x3F, cy)
	cy++
	tracer.OnCycle(KIND_READ, 0x0802, 0x06, cy) // F$Exit
	cy++

	for i := 0; i < 12; i++ {
		tracer.OnCycle(KIND_WRITE, uint16(0x0FEE-i), 0x00, cy)
		cy++
	}

	out := buf.String()
	if !strings.Contains(out, "F$Exit") || !strings.Contains(out, "(never returns)") {
		t.Fatalf("Expected F$Exit and (never returns), got:\n%s", out)
	}
	if len(tracer.Pending) != 0 {
		t.Fatalf("Non-returning call should not be placed in Pending, got %d", len(tracer.Pending))
	}
}

func TestOs9TracerHardwareInterruptRti(t *testing.T) {
	var buf bytes.Buffer
	tracer := NewOs9Tracer(&buf)

	// Simulate RTI from an IRQ handler returning to $1234 without prior SWI2
	cy := uint64(2000)
	tracer.OnCycle(KIND_FIC, 0x0150, 0x3B, cy)
	cy++
	tracer.OnCycle(KIND_READ, 0x0151, 0x00, cy)
	cy++

	reads := []struct {
		addr uint16
		data byte
	}{
		{0x01F0, 0x80}, // CC (E=1)
		{0x01F1, 0x00}, {0x01F2, 0x00}, {0x01F3, 0x00},
		{0x01F4, 0x00}, {0x01F5, 0x00}, {0x01F6, 0x00},
		{0x01F7, 0x00}, {0x01F8, 0x00}, {0x01F9, 0x00},
		{0x01FA, 0x12}, {0x01FB, 0x34}, // PC = $1234
	}
	for _, r := range reads {
		tracer.OnCycle(KIND_READ, r.addr, r.data, cy)
		cy++
	}

	out := buf.String()
	if strings.Contains(out, "SWI2") {
		t.Fatalf("Hardware interrupt RTI should not mention SWI2, got:\n%s", out)
	}
	if !strings.Contains(out, "returning from interrupt to $1234") {
		t.Fatalf("Expected 'returning from interrupt to $1234', got:\n%s", out)
	}
}
