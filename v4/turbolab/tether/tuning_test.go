package main

import (
	"encoding/binary"
	"testing"
)

func TestParseTuningFlag(t *testing.T) {
	// 1. Empty string returns defaults
	def, err := ParseTuningFlag("")
	if err != nil {
		t.Fatalf("Parse empty failed: %v", err)
	}
	if def.Mhz != 250 || def.K[1] != 9 || def.K[2] != 19 || def.K[3] != 11 || def.K[4] != 8 ||
		def.T[1] != 16 || def.T[2] != 0 || def.T[3] != 22 || def.T[4] != 3 || def.T[5] != 12 {
		t.Fatalf("Default mismatch: %+v", def)
	}

	// 2. 8 positional numbers: MHZ, K1, K2, T1, T2, T3, T4, T5
	p8, err := ParseTuningFlag("200,8,16,13,1,18,2,10")
	if err != nil {
		t.Fatalf("Parse 8 failed: %v", err)
	}
	if p8.Mhz != 200 || p8.K[1] != 8 || p8.K[2] != 16 || p8.T[1] != 13 || p8.T[2] != 1 ||
		p8.T[3] != 18 || p8.T[4] != 2 || p8.T[5] != 10 {
		t.Fatalf("8-param mismatch: %+v", p8)
	}
	// K3 and K4 should remain default (11 and 8)
	if p8.K[3] != 11 || p8.K[4] != 8 {
		t.Fatalf("K3/K4 unexpected: K3=%d, K4=%d", p8.K[3], p8.K[4])
	}

	// 3. 10 positional numbers: MHZ, K1, K2, K3, K4, T1, T2, T3, T4, T5
	p10, err := ParseTuningFlag("300,10,21,12,9,18,0,24,4,14")
	if err != nil {
		t.Fatalf("Parse 10 failed: %v", err)
	}
	if p10.Mhz != 300 || p10.K[1] != 10 || p10.K[2] != 21 || p10.K[3] != 12 || p10.K[4] != 9 ||
		p10.T[1] != 18 || p10.T[2] != 0 || p10.T[3] != 24 || p10.T[4] != 4 || p10.T[5] != 14 {
		t.Fatalf("10-param mismatch: %+v", p10)
	}

	// 4. Named parameters
	pNamed, err := ParseTuningFlag("MHZ=200,K1=7,T3=19,T5=15")
	if err != nil {
		t.Fatalf("Parse named failed: %v", err)
	}
	if pNamed.Mhz != 200 || pNamed.K[1] != 7 || pNamed.T[3] != 19 || pNamed.T[5] != 15 {
		t.Fatalf("Named mismatch: %+v", pNamed)
	}
	// Untouched should have defaults
	if pNamed.K[2] != 19 || pNamed.T[1] != 16 {
		t.Fatalf("Untouched mismatch: K2=%d, T1=%d", pNamed.K[2], pNamed.T[1])
	}

	// 5. Binary encode check
	enc := pNamed.Encode()
	if len(enc) != 84 {
		t.Fatalf("Encode len=%d, expected 84", len(enc))
	}
	if binary.LittleEndian.Uint32(enc[0:4]) != 200 {
		t.Fatalf("Encoded MHZ mismatch")
	}
	if binary.LittleEndian.Uint32(enc[1*4:2*4]) != 7 {
		t.Fatalf("Encoded K1 mismatch")
	}
}
