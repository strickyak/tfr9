package lib

import (
	"testing"
)

func TestDecode(t *testing.T) {
	tests := []struct {
		name       string
		mem        []byte
		disasm     string
		numBytes   int
		numCycles  int
		cycleCodes string
		ok         bool
	}{
		{"NOP", []byte{0x12}, "NOP", 1, 2, "o-", true},
		{"LDA Immediate", []byte{0x86, 0x12}, "LDA #$12", 2, 2, "op", true},
		{"LDA Direct", []byte{0x96, 0x12}, "LDA <$12", 2, 4, "op-r", true},
		{"LDA Extended", []byte{0xB6, 0x12, 0x34}, "LDA >$1234", 3, 5, "opp-r", true},
		{"LDD Immediate", []byte{0xCC, 0x12, 0x34}, "LDD #$1234", 3, 3, "opp", true},
		{"STA Direct", []byte{0x97, 0x12}, "STA <$12", 2, 4, "op-w", true},
		{"INC Extended", []byte{0x7C, 0x12, 0x34}, "INC >$1234", 3, 7, "opp-r-w", true},

		// Indexed
		{"LDA ,X", []byte{0xA6, 0x84}, "LDA ,X", 2, 4, "op-r", true},
		{"LDA 5,X", []byte{0xA6, 0x05}, "LDA 5,X", 2, 5, "op--r", true},
		{"LDA 16-bit offset", []byte{0xA6, 0x89, 0x12, 0x34}, "LDA 4660,X", 4, 8, "oppp---r", true},
		{"LDA [,X]", []byte{0xA6, 0x94}, "LDA [,X]", 2, 7, "oprr--r", true},

		// Push/Pull
		{"PSHS A,B", []byte{0x34, 0x06}, "PSHS A,B", 2, 7, "op--ww-", true},
		{"PULS PC", []byte{0x35, 0x80}, "PULS PC", 2, 7, "op--rr-", true},

		// Branches
		{"BRA", []byte{0x20, 0x05}, "BRA 5", 2, 3, "op-", true},
		{"LBRA", []byte{0x16, 0x00, 0x05}, "LBRA 5", 3, 5, "opp--", true},
		{"LBEQ", []byte{0x10, 0x27, 0x00, 0x05}, "LBEQ 5", 4, 6, "oopp--", true},

		// Two-byte opcode
		{"CMPD Immed", []byte{0x10, 0x83, 0x12, 0x34}, "CMPD #$1234", 4, 4, "oopp", true},
		{"CMPU Immed", []byte{0x11, 0x83, 0x12, 0x34}, "CMPU #$1234", 4, 4, "oopp", true},

		// Misc
		{"TFR A,B", []byte{0x1F, 0x89}, "TFR A,B", 2, 6, "op----", true},
		{"CWAI", []byte{0x3C, 0xFF}, "CWAI #$FF", 2, 20, "op-wwwwwwwwwwww-----", true},

		// Error cases
		{"Short mem", []byte{0xB6, 0x12}, "", 0, 0, "", false},
		{"Short indexed", []byte{0xA6, 0x89, 0x12}, "", 0, 0, "", false},
		{"Invalid opcode", []byte{0x01}, "", 0, 0, "", false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			disasm, numBytes, numCycles, cycleCodes, ok := Decode(tt.mem)

			if ok != tt.ok {
				t.Errorf("Decode() ok = %v, want %v", ok, tt.ok)
				return
			}
			if !ok {
				return
			}

			if disasm != tt.disasm {
				t.Errorf("Decode() disasm = %q, want %q", disasm, tt.disasm)
			}
			if numBytes != tt.numBytes {
				t.Errorf("Decode() numBytes = %v, want %v", numBytes, tt.numBytes)
			}
			if numCycles != tt.numCycles {
				t.Errorf("Decode() numCycles = %v, want %v", numCycles, tt.numCycles)
			}
			if cycleCodes != tt.cycleCodes {
				t.Errorf("Decode() cycleCodes = %q, want %q", cycleCodes, tt.cycleCodes)
			}
		})
	}
}
