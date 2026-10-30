package main

import (
	"bytes"
	"os"
	"strings"
	"testing"
)

func TestFormatCC(t *testing.T) {
	tests := []struct {
		cc   byte
		want string
	}{
		{0x00, "efhinzvc"},
		{0xFF, "EFHINZVC"},
		{0x80, "Efhinzvc"}, // E
		{0x40, "eFhinzvc"}, // F
		{0x20, "efHinzvc"}, // H
		{0x10, "efhInzvc"}, // I
		{0x08, "efhiNzvc"}, // N
		{0x04, "efhinZvc"}, // Z
		{0x02, "efhinzVc"}, // V
		{0x01, "efhinzvC"}, // C
		{0xD0, "EFhInzvc"}, // E + F + I
	}

	for _, tt := range tests {
		got := formatCC(tt.cc)
		if got != tt.want {
			t.Errorf("formatCC(0x%02X) = %q, want %q", tt.cc, got, tt.want)
		}
	}
}

func TestDumpHexLines(t *testing.T) {
	ram := make([]byte, 256)
	ram[0x10] = 0x12
	ram[0x11] = 0x34
	ram[0x1F] = 0x78

	var buf bytes.Buffer
	dumpHexLines(&buf, ram, 0, 0x30)
	out := buf.String()
	if !strings.Contains(out, "$0010:") {
		t.Errorf("expected dumpHexLines to contain '$0010:', got:\n%s", out)
	}
	if !strings.Contains(out, "12 34") {
		t.Errorf("expected dumpHexLines to contain '12 34', got:\n%s", out)
	}
}

func TestDump6309PanicStateWithNil(t *testing.T) {
	// Should not panic even if ram is nil
	Dump6309PanicState(nil)
}

func TestDump6309PanicStateWithCoco1Ram(t *testing.T) {
	ram := new(Coco1Ram)
	// Vectors: RESET = $1000, SWI2 = $2000
	ram.Poke1(0xFFFE, 0x10)
	ram.Poke1(0xFFFF, 0x00)
	ram.Poke1(0xFFF4, 0x20)
	ram.Poke1(0xFFF5, 0x00)

	// Direct Page vectors
	ram.Poke1(L1_D_SWI2, 0x21)
	ram.Poke1(L1_D_SWI2+1, 0x00)

	// Set D.Proc = $0200
	ram.Poke1(L1_D_Proc, 0x02)
	ram.Poke1(L1_D_Proc+1, 0x00)

	// Process descriptor at $0200:
	// PID = 1, PPID = 0, SP = $03F0, PModul = $0400
	ram.Poke1(0x0200+L1_P_ID, 1)
	ram.Poke1(0x0200+L1_P_PID, 0)
	ram.Poke1(0x0200+L1_P_SP, 0x03)
	ram.Poke1(0x0200+L1_P_SP+1, 0xF0)
	ram.Poke1(0x0200+L1_P_PModul, 0x04)
	ram.Poke1(0x0200+L1_P_PModul+1, 0x00)

	// Module at $0400: sync $87CD, name at offset 9 -> "Shell" with high-bit on 'l'
	ram.Poke1(0x0400, 0x87)
	ram.Poke1(0x0401, 0xCD)
	ram.Poke1(0x0404, 0x00)
	ram.Poke1(0x0405, 0x09) // name offset
	ram.Poke1(0x0409, 'S')
	ram.Poke1(0x040A, 'h')
	ram.Poke1(0x040B, 'e')
	ram.Poke1(0x040C, 'l')
	ram.Poke1(0x040D, 'l'|0x80)

	// Stack frame at $03F0:
	// CC = $D0 (EFhInzvc), A = $12, B = $34, DP = $00, X = $1000, Y = $2000, U = $3000, PC = $4000
	ram.Poke1(0x03F0+L1_R_CC, 0xD0)
	ram.Poke1(0x03F0+L1_R_A, 0x12)
	ram.Poke1(0x03F0+L1_R_B, 0x34)
	ram.Poke1(0x03F0+L1_R_DP, 0x00)
	ram.Poke1(0x03F0+L1_R_X, 0x10)
	ram.Poke1(0x03F0+L1_R_X+1, 0x00)
	ram.Poke1(0x03F0+L1_R_Y, 0x20)
	ram.Poke1(0x03F0+L1_R_Y+1, 0x00)
	ram.Poke1(0x03F0+L1_R_U, 0x30)
	ram.Poke1(0x03F0+L1_R_U+1, 0x00)
	ram.Poke1(0x03F0+L1_R_PC, 0x40)
	ram.Poke1(0x03F0+L1_R_PC+1, 0x00)

	// Instructions at PC $4000: NOP ($12) NOP ($12)
	ram.Poke1(0x4000, 0x12)
	ram.Poke1(0x4001, 0x12)

	// Run Dump6309PanicState
	Dump6309PanicState(ram)

	// Check that /tmp/fault.img was written
	info, err := os.Stat("/tmp/fault.img")
	if err != nil {
		t.Fatalf("failed to stat /tmp/fault.img: %v", err)
	}
	if info.Size() != 65536 {
		t.Errorf("expected /tmp/fault.img size 65536, got %d", info.Size())
	}
}
