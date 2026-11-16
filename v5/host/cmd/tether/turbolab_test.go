package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestTurbolabTraceFormatterAndOs9Swi2(t *testing.T) {
	ram := make([]byte, 65536)
	// Build a minimal OS-9 module header at $E000 named "Kernel" (size $0040, namoff $000D)
	base := 0xE000
	ram[base+0] = 0x87
	ram[base+1] = 0xCD
	ram[base+2] = 0x00
	ram[base+3] = 0x40
	ram[base+4] = 0x00
	ram[base+5] = 0x0D
	ram[base+6] = 0xF1
	ram[base+7] = 0x81
	var p byte = 0xFF
	for j := 0; j < 8; j++ {
		p ^= ram[base+j]
	}
	ram[base+8] = p
	copy(ram[base+0x0D:], "Kerne")
	ram[base+0x12] = 'l' | 0x80
	ram[base+0x3D] = 0x12
	ram[base+0x3E] = 0x34
	ram[base+0x3F] = 0x56

	// Put OS-9 string "Init" (high-bit terminated) at $EF00 for F$Link ($00)
	copy(ram[0xEF00:], "Ini")
	ram[0xEF03] = 't' | 0x80

	mods := ScanImageForOs9Modules(ram)
	if len(mods) != 1 || mods[0].Name != "Kernel" || mods[0].BaseAddr != 0xE000 {
		t.Fatalf("expected 1 Kernel module at $E000, got %+v", mods)
	}

	var out bytes.Buffer
	tf := &TraceFormatter{
		Out:          &out,
		Modules:      mods,
		TraceBitmask: TL_TRACE_X | TL_TRACE_R | TL_TRACE_I,
		Os9Tracer:    NewOs9Tracer(&out, ram),
	}

	// Cycle #1: Reset vector high ($FFFE) with BS=1
	tf.FormatCycle(TL_KIND_READ|TL_FLAG_BS, 0xFFFE, 0xE0, 1)
	// Cycle #4: FIC inside Kernel ($E01B)
	tf.FormatCycle(TL_KIND_FIC, 0xE01B, 0x4F, 4)

	// Simulate SWI2 ($10 $3F $00 = F$Link) at $E020 pushing 12-byte stack frame at $03F4..$03FF
	tf.FormatCycle(TL_KIND_FIC, 0xE020, 0x10, 10)
	tf.FormatCycle(TL_KIND_OPCODE_CONT, 0xE021, 0x3F, 11)
	tf.FormatCycle(TL_KIND_READ, 0xE022, 0x00, 12) // F$Link = $00
	// 12 writes from PC.lo down to CC: PC=$E023, U=$0100, Y=$0200, X=$EF00 ("Init"), DP=$00, B=$00, A=$01, CC=$80
	frame := [12]byte{0x80, 0x01, 0x00, 0x00, 0xEF, 0x00, 0x02, 0x00, 0x01, 0x00, 0xE0, 0x23}
	for i := 0; i < 12; i++ {
		tf.FormatCycle(TL_KIND_WRITE, uint16(0x03FF-i), frame[11-i], uint64(13+i))
	}

	// Simulate matching RTI ($3B) popping the 12-byte stack frame from $03F4..$03FF
	tf.FormatCycle(TL_KIND_FIC, 0xE030, 0x3B, 30)
	for i := 0; i < 12; i++ {
		tf.FormatCycle(TL_KIND_READ, uint16(0x03F4+i), frame[i], uint64(31+i))
	}

	got := out.String()
	for _, want := range []string{
		"r FFFE E0 #1;s reset vector (high)",
		`x E01B 4F #4; "kernel.0040123456"+001b`,
		`i SWI2 _1_ #10 at $E020: $00 = F$Link`,
		`"Init"`,
		`i RTI  _1_ #30 returning to SWI2 $00 (F$Link)`,
	} {
		if !strings.Contains(got, want) {
			t.Errorf("trace output missing %q; got:\n%s", want, got)
		}
	}
}
