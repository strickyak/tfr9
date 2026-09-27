package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestTokenizeAndDetokenize(t *testing.T) {
	testCases := []string{
		`HSCREEN 2: HCLS: FOR I=1 TO 7: READ C: PALETTE I,C: NEXT`,
		`FOR B=1 TO 7: FOR R=155-(B-1)*10 TO 155-B*10+1 STEP -1: HCIRCLE (160,191),R, B: NEXT R,B`,
		`GOTO 30`,
		`DATA 7,38,36,18,31,11,9`,
		`PRINT "HELLO WORLD": REM A COMMENT`,
		`' ANOTHER COMMENT`,
		`WIDTH 80: LOCATE 0,0: HPRINT "TEST"`,
	}

	for _, tc := range testCases {
		toks, err := TokenizeLine(tc)
		if err != nil {
			t.Fatalf("failed to tokenize %q: %v", tc, err)
		}

		detok := DetokenizeLine(toks)
		t.Logf("Orig:  %s", tc)
		t.Logf("Hex:   %x", toks)
		t.Logf("Detok: %s", detok)

		// Verify key tokens exist in output
		if strings.Contains(tc, "HSCREEN") && !strings.Contains(detok, "HSCREEN") {
			t.Errorf("detokenized line missing HSCREEN: %s", detok)
		}
		if strings.Contains(tc, "PALETTE") && !strings.Contains(detok, "PALETTE") {
			t.Errorf("detokenized line missing PALETTE: %s", detok)
		}
		if strings.Contains(tc, "HCIRCLE") && !strings.Contains(detok, "HCIRCLE") {
			t.Errorf("detokenized line missing HCIRCLE: %s", detok)
		}
	}
}

func TestParseAsciiAndTokenizedRoundtrip(t *testing.T) {
	asciiProg := `10 HSCREEN 2: HCLS: FOR I=1 TO 7: READ C: PALETTE I,C: NEXT
20 FOR B=1 TO 7: FOR R=155-(B-1)*10 TO 155-B*10+1 STEP -1: HCIRCLE (160,191),R, B: NEXT R,B
30 GOTO 30
90 DATA 7,38,36,18,31,11,9`

	txttab := uint(0x1E00)
	lines, err := ParseAsciiBasic(asciiProg)
	if err != nil {
		t.Fatalf("ParseAsciiBasic failed: %v", err)
	}
	if len(lines) != 4 {
		t.Fatalf("expected 4 lines, got %d", len(lines))
	}

	programBytes, newEnd := CompileBasicProgram(lines, txttab)
	if len(programBytes) == 0 {
		t.Fatalf("compiled programBytes is empty")
	}
	if newEnd != txttab+uint(len(programBytes)) {
		t.Fatalf("newEnd mismatch: got %x, expected %x", newEnd, txttab+uint(len(programBytes)))
	}

	// Verify end-of-program marker (0x00 0x00)
	if programBytes[len(programBytes)-1] != 0 || programBytes[len(programBytes)-2] != 0 {
		t.Fatalf("programBytes missing trailing double null")
	}

	// Now parse back as tokenized program
	tokenizedLines, err := ParseTokenizedBasic(programBytes)
	if err != nil {
		t.Fatalf("ParseTokenizedBasic failed on compiled binary: %v", err)
	}
	if len(tokenizedLines) != len(lines) {
		t.Fatalf("roundtrip line count mismatch: got %d, expected %d", len(tokenizedLines), len(lines))
	}

	for i := range lines {
		if lines[i].LineNum != tokenizedLines[i].LineNum {
			t.Errorf("line %d number mismatch: %d vs %d", i, lines[i].LineNum, tokenizedLines[i].LineNum)
		}
		if !bytes.Equal(lines[i].Tokens, tokenizedLines[i].Tokens) {
			t.Errorf("line %d tokens mismatch: %x vs %x", i, lines[i].Tokens, tokenizedLines[i].Tokens)
		}
	}
}

func TestDiskBasicPreamble(t *testing.T) {
	asciiProg := "10 PRINT \"HI\"\n20 END\n"
	lines, _ := ParseAsciiBasic(asciiProg)
	rawBytes, _ := CompileBasicProgram(lines, 0x1E00)

	// Prepend Disk Basic 3-byte header ($FF, len_hi, len_lo)
	preamble := []byte{0xFF, byte(len(rawBytes) >> 8), byte(len(rawBytes) & 0xFF)}
	diskFileBytes := append(preamble, rawBytes...)

	if !IsTokenizedBasic(diskFileBytes) {
		t.Fatalf("expected IsTokenizedBasic to be true for disk file with preamble")
	}

	parsed, err := ParseTokenizedBasic(diskFileBytes)
	if err != nil {
		t.Fatalf("failed to parse disk file with preamble: %v", err)
	}
	if len(parsed) != 2 {
		t.Fatalf("expected 2 lines, got %d", len(parsed))
	}
}
