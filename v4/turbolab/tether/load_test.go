package main

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

func TestParseDecb_Pi(t *testing.T) {
	path := filepath.Join("..", "data", "bootable", "pi.decb")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("Failed to read %s: %v", path, err)
	}

	ramImage, entryPoint, err := ParseDecb(data)
	if err != nil {
		t.Fatalf("ParseDecb failed: %v", err)
	}

	if len(ramImage) != 65536 {
		t.Fatalf("Expected 65536 bytes, got %d", len(ramImage))
	}

	// pi.decb entry point is $1000
	if entryPoint != 0x1000 {
		t.Fatalf("Expected entry point $1000, got $%04X", entryPoint)
	}

	// RESET vector at $FFFE..$FFFF must be $1000
	resetVector := binary.BigEndian.Uint16(ramImage[0xFFFE:0x10000])
	if resetVector != 0x1000 {
		t.Fatalf("Expected RESET vector $1000, got $%04X", resetVector)
	}

	// Verify payload at $1000
	// Byte 5.. in pi.decb is: 0x4F, 0x1F, 0x8B, 0x8E, 0x02, 0x00...
	expectedPayload := data[5 : 5+280]
	actualPayload := ramImage[0x1000 : 0x1000+280]
	if !bytes.Equal(expectedPayload, actualPayload) {
		t.Fatalf("Memory at $1000 does not match payload")
	}

	// Verify unwritten memory regions remain 0
	if ramImage[0x0000] != 0 || ramImage[0x0FFF] != 0 || ramImage[0x1118] != 0 || ramImage[0xFFFD] != 0 {
		t.Fatalf("Unexpected non-zero bytes in unwritten memory")
	}
}

func TestPrepareMemoryImage_Decb(t *testing.T) {
	path := filepath.Join("..", "data", "bootable", "pi.decb")
	ramImage, err := PrepareMemoryImage([]string{path})
	if err != nil {
		t.Fatalf("PrepareMemoryImage failed: %v", err)
	}

	if len(ramImage) != 65536 {
		t.Fatalf("Expected 65536 bytes, got %d", len(ramImage))
	}

	resetVector := binary.BigEndian.Uint16(ramImage[0xFFFE:0x10000])
	if resetVector != 0x1000 {
		t.Fatalf("Expected RESET vector $1000, got $%04X", resetVector)
	}
}

func TestParseDecb_MultipleDataBlocks(t *testing.T) {
	// Construct a DECB with two data blocks and one exec block:
	// Block 1: addr $2000, 4 bytes [0x11, 0x22, 0x33, 0x44]
	// Block 2: addr $4000, 3 bytes [0xAA, 0xBB, 0xCC]
	// Exec block: entry point $2000
	var buf bytes.Buffer

	// Block 1 header
	buf.WriteByte(0x00)
	buf.Write([]byte{0x00, 0x04}) // len = 4
	buf.Write([]byte{0x20, 0x00}) // addr = $2000
	buf.Write([]byte{0x11, 0x22, 0x33, 0x44})

	// Block 2 header
	buf.WriteByte(0x00)
	buf.Write([]byte{0x00, 0x03}) // len = 3
	buf.Write([]byte{0x40, 0x00}) // addr = $4000
	buf.Write([]byte{0xAA, 0xBB, 0xCC})

	// Exec block
	buf.WriteByte(0xFF)
	buf.Write([]byte{0x00, 0x00}) // len = 0
	buf.Write([]byte{0x20, 0x00}) // exec = $2000

	ramImage, entryPoint, err := ParseDecb(buf.Bytes())
	if err != nil {
		t.Fatalf("ParseDecb failed: %v", err)
	}

	if entryPoint != 0x2000 {
		t.Fatalf("Expected entry point $2000, got $%04X", entryPoint)
	}

	if !bytes.Equal(ramImage[0x2000:0x2004], []byte{0x11, 0x22, 0x33, 0x44}) {
		t.Fatalf("Block 1 payload mismatch")
	}
	if !bytes.Equal(ramImage[0x4000:0x4003], []byte{0xAA, 0xBB, 0xCC}) {
		t.Fatalf("Block 2 payload mismatch")
	}

	resetVector := binary.BigEndian.Uint16(ramImage[0xFFFE:0x10000])
	if resetVector != 0x2000 {
		t.Fatalf("Expected RESET vector $2000, got $%04X", resetVector)
	}
}

func TestParseDecb_ValidationErrors(t *testing.T) {
	// 1. Too short
	shortData := []byte{0x00, 0x00, 0x00, 0x00}
	if _, _, err := ParseDecb(shortData); err == nil {
		t.Errorf("Expected error for short data, got nil")
	}

	// 2. Does not start with $00
	badStart := []byte{0x01, 0x00, 0x01, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x10, 0x00}
	if _, _, err := ParseDecb(badStart); err == nil {
		t.Errorf("Expected error for non-$00 start, got nil")
	}

	// 3. Bad trailer (fifth byte before end not $FF)
	badTrailer := []byte{0x00, 0x00, 0x01, 0x10, 0x00, 0x55, 0x00, 0x00, 0x00, 0x10, 0x00}
	if _, _, err := ParseDecb(badTrailer); err == nil {
		t.Errorf("Expected error for non-$FF trailer, got nil")
	}

	// 4. Truncated payload
	truncated := []byte{
		0x00, 0x00, 0x10, 0x10, 0x00, // length 16, but only 2 bytes follow
		0x11, 0x22,
		0xFF, 0x00, 0x00, 0x10, 0x00,
	}
	if _, _, err := ParseDecb(truncated); err == nil {
		t.Errorf("Expected error for truncated payload, got nil")
	}

	// 5. Memory overflow (> 64KB)
	overflow := []byte{
		0x00, 0x01, 0x00, 0xFF, 0xF0, // addr $FFF0 + len $100 = $100F0 > 64KB
	}
	overflow = append(overflow, make([]byte, 256)...)
	overflow = append(overflow, 0xFF, 0x00, 0x00, 0x10, 0x00)
	if _, _, err := ParseDecb(overflow); err == nil {
		t.Errorf("Expected error for memory overflow, got nil")
	}

	// 6. Trailing data after exec block
	trailing := []byte{
		0x00, 0x00, 0x01, 0x10, 0x00, 0x42,
		0xFF, 0x00, 0x00, 0x10, 0x00,
		0x00, 0x00, // unexpected trailing bytes
	}
	if _, _, err := ParseDecb(trailing); err == nil {
		t.Errorf("Expected error for trailing data, got nil")
	}
}

func TestPrepareMemoryImage_MixErrors(t *testing.T) {
	// Cannot mix .img and .decb
	if _, err := PrepareMemoryImage([]string{"foo.img", "bar.decb"}); err == nil {
		t.Errorf("Expected error for mixing .img and .decb, got nil")
	}

	// Cannot mix .decb and OS-9 module
	if _, err := PrepareMemoryImage([]string{"foo.decb", "krn"}); err == nil {
		t.Errorf("Expected error for mixing .decb and OS-9 module, got nil")
	}

	// Cannot pass multiple .decb files
	if _, err := PrepareMemoryImage([]string{"foo.decb", "bar.decb"}); err == nil {
		t.Errorf("Expected error for multiple .decb files, got nil")
	}

	// Cannot pass empty files list
	if _, err := PrepareMemoryImage([]string{}); err == nil {
		t.Errorf("Expected error for empty files list, got nil")
	}
}
