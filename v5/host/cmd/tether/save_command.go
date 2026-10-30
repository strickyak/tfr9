package main

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

func init() {
	RegisterUCommand("save", handleSaveCommand)
}

func handleSaveCommand(args string) {
	err := ExecuteSaveCommand(args)
	if err != nil {
		fmt.Printf("\r*** %v\r\n", err)
	}
}

// ExecuteSaveCommand executes the ~save command to save a BASIC program from RAM to disk.
// If filename ends in .txt, it saves in pure ASCII text mode.
// Other filenames (e.g. .bas) use the optimized tokenized format with Disk BASIC preamble ($FF + 2-byte length).
func ExecuteSaveCommand(args string) error {
	filename := strings.TrimSpace(args)
	filename = strings.Trim(filename, "\"'")
	if filename == "" {
		return fmt.Errorf("Usage: ~save <filename.bas|filename.txt>")
	}

	ch := GetChannelToPico()
	if ch == nil {
		return fmt.Errorf("Error: not connected to Pico")
	}

	if the_ram == nil {
		return fmt.Errorf("Error: the_ram is nil")
	}

	// Read zero-page pointers from Pico to determine TXTTAB and VARTAB.
	// We read 32 bytes covering $0010 - $002F.
	zpPhys := int(the_ram.Physical(0x0010))
	zpData, err := fetchRamRange(ch, zpPhys, 32)
	if err != nil {
		return fmt.Errorf("Error reading zero-page from Pico: %v", err)
	}
	if len(zpData) < 32 {
		return fmt.Errorf("Error: short read of zero-page from Pico (%d bytes)", len(zpData))
	}

	// TXTTAB is at $0019-$001A (offset 9 in zpData: 0x19 - 0x10 = 9)
	txttab := (uint(zpData[0x19-0x10]) << 8) | uint(zpData[0x1A-0x10])
	// VARTAB is at $001B-$001C (offset 11 in zpData: 0x1B - 0x10 = 11)
	vartab := (uint(zpData[0x1B-0x10]) << 8) | uint(zpData[0x1C-0x10])

	if txttab == 0 {
		return fmt.Errorf("Error: TXTTAB is 0; BASIC does not appear to be running")
	}

	if vartab <= txttab+2 {
		return fmt.Errorf("No program in RAM to save")
	}

	// Check link pointer at TXTTAB to confirm program is not empty (0x0000 means empty)
	txtPhys := int(the_ram.Physical(txttab))
	firstWord, err := fetchRamRange(ch, txtPhys, 2)
	if err != nil {
		return fmt.Errorf("Error reading TXTTAB from Pico: %v", err)
	}
	if len(firstWord) < 2 || (firstWord[0] == 0 && firstWord[1] == 0) {
		return fmt.Errorf("No program in RAM to save")
	}

	progLen := vartab - txttab
	if progLen > 64*1024 {
		return fmt.Errorf("Error: invalid program length (%d bytes)", progLen)
	}

	// Fetch entire program from Pico RAM
	progBytes, err := fetchRamRange(ch, txtPhys, int(progLen))
	if err != nil {
		return fmt.Errorf("Error reading program from Pico RAM: %v", err)
	}
	if len(progBytes) < int(progLen) {
		return fmt.Errorf("Error: short read of program from RAM (expected %d, got %d)", progLen, len(progBytes))
	}

	// Mirror program bytes into the_ram
	for i, b := range progBytes {
		the_ram.Poke1(txttab+uint(i), b)
	}

	// Parse lines so we verify program structure and can detokenize if ASCII mode
	lines, err := ParseTokenizedBasic(progBytes)
	if err != nil {
		return fmt.Errorf("Error parsing BASIC program from RAM: %v", err)
	}

	var fileData []byte
	isAscii := strings.HasSuffix(strings.ToLower(filename), ".txt")

	if isAscii {
		// Pure ASCII text format: one line per BASIC line
		var buf bytes.Buffer
		for _, l := range lines {
			detok := DetokenizeLine(l.Tokens)
			fmt.Fprintf(&buf, "%d %s\n", l.LineNum, detok)
		}
		fileData = buf.Bytes()
	} else {
		// Optimized tokenized binary format with Disk BASIC preamble ($FF + 2-byte length)
		fileData = make([]byte, 3+len(progBytes))
		fileData[0] = 0xFF
		fileData[1] = byte(len(progBytes) >> 8)
		fileData[2] = byte(len(progBytes) & 0xFF)
		copy(fileData[3:], progBytes)
	}

	// Ensure directory exists if path contains directory separators
	if dir := filepath.Dir(filename); dir != "." && dir != "" {
		_ = os.MkdirAll(dir, 0755)
	}

	if err := os.WriteFile(filename, fileData, 0644); err != nil {
		return fmt.Errorf("File write error: %v", err)
	}

	fmt.Printf("\r*** Saved %d lines (%d bytes) to %s\r\n", len(lines), len(fileData), filename)
	return nil
}
