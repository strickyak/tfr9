package main

import (
	"testing"
)

func TestDecompressFIC(t *testing.T) {
	oldRam := the_ram
	defer func() { the_ram = oldRam }()
	the_ram = new(Coco1Ram)

	// Stream 1:
	// 00 00 (FIC prefix)
	// 11    (Old Read Next -> addr 1)
	// 11    (Old Read Next, non-FIC -> addr 2)
	// 00 10 (Padding/Sentinel: 00 10)
	compressed1 := []byte{0x0F, 0x20}

	ResetDecompressCycles()
	cycles1 := DecompressCycles(compressed1, 2)

	if len(cycles1) != 2 {
		t.Fatalf("expected 2 cycles, got %d", len(cycles1))
	}

	verb0 := (cycles1[0] >> 24) & 0xFF
	addr0 := (cycles1[0] >> 8) & 0xFFFF
	if verb0 != 5 { // dcFifoFIC
		t.Errorf("cycle 0: expected verb 5 (dcFifoFIC), got %d", verb0)
	}
	if addr0 != 1 {
		t.Errorf("cycle 0: expected addr 1, got %d", addr0)
	}

	verb1 := (cycles1[1] >> 24) & 0xFF
	addr1 := (cycles1[1] >> 8) & 0xFFFF
	if verb1 != 1 { // dcFifoRead
		t.Errorf("cycle 1: expected verb 1 (dcFifoRead), got %d", verb1)
	}
	if addr1 != 2 {
		t.Errorf("cycle 1: expected addr 2, got %d", addr1)
	}

	// Stream 2: New Read with FIC prefix
	// 00 00 (FIC prefix)
	// 01    (New Read)
	// 01    (Same address -> addr 0)
	// 0x55  (Data byte)
	// 10    (Write)
	// 10    (Incr -> addr 1)
	// 0xAA  (Data byte)
	//
	// BitWriter layout:
	// 00 00 01 01 01010101 (0x05, 0x55)
	// 10 10 10101010       (0xAA, 0xAA)
	// 00 10 (sentinel)     (0x20)
	compressed2 := []byte{0x05, 0x55, 0xAA, 0xAA, 0x20}

	ResetDecompressCycles()
	cycles2 := DecompressCycles(compressed2, 2)

	if len(cycles2) != 2 {
		t.Fatalf("expected 2 cycles, got %d", len(cycles2))
	}

	c0Verb := (cycles2[0] >> 24) & 0xFF
	c0Addr := (cycles2[0] >> 8) & 0xFFFF
	c0Data := cycles2[0] & 0xFF
	if c0Verb != 5 {
		t.Errorf("expected verb 5 (FIC), got %d", c0Verb)
	}
	if c0Addr != 0 {
		t.Errorf("expected addr 0, got %d", c0Addr)
	}
	if c0Data != 0x55 {
		t.Errorf("expected data 0x55, got 0x%02x", c0Data)
	}

	c1Verb := (cycles2[1] >> 24) & 0xFF
	c1Addr := (cycles2[1] >> 8) & 0xFFFF
	c1Data := cycles2[1] & 0xFF
	if c1Verb != 3 { // dcFifoWrite
		t.Errorf("expected verb 3 (Write), got %d", c1Verb)
	}
	if c1Addr != 1 {
		t.Errorf("expected addr 1, got %d", c1Addr)
	}
	if c1Data != 0xAA {
		t.Errorf("expected data 0xAA, got 0x%02x", c1Data)
	}

	// Stream 3: Round-trip from C++ CompressCycles
	// 1. Old Read Next (FIC): FG0BG_FIC, $8001
	// 2. Old Read Next (non-FIC): FG2BG_READ, $8002
	// 3. New Read (FIC): FG0BG_FIC, $0100, $55
	// 4. Write: FG2BG_WRITE, $0101, $AA
	// 5. Old Read Decr (non-FIC): FG2BG_READ, $8001 (zone fallback)
	compressed3 := []byte{0x03, 0x80, 0x00, 0x1C, 0x1C, 0x01, 0x00, 0x15, 0x6C, 0x01, 0x00, 0x6A, 0x8E, 0x20}

	ResetDecompressCycles()
	cycles3 := DecompressCycles(compressed3, 5)

	if len(cycles3) != 5 {
		t.Fatalf("expected 5 cycles from C++ roundtrip, got %d", len(cycles3))
	}

	expected := []struct {
		verb uint32
		addr uint16
		data byte
	}{
		{5, 0x8001, 0x00},
		{1, 0x8002, 0x00},
		{5, 0x0100, 0x55},
		{3, 0x0101, 0xAA},
		{1, 0x8001, 0x00},
	}

	for i, exp := range expected {
		v := (cycles3[i] >> 24) & 0xFF
		a := uint16((cycles3[i] >> 8) & 0xFFFF)
		d := byte(cycles3[i] & 0xFF)
		if v != exp.verb || a != exp.addr || (exp.verb != 1 && exp.verb != 5 && d != exp.data) {
			t.Errorf("cycle %d: expected verb=%d addr=%04x data=%02x; got verb=%d addr=%04x data=%02x",
				i, exp.verb, exp.addr, exp.data, v, a, d)
		}
	}
}
