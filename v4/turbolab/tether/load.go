package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"
)

// OS-9 vectors copied to $FFF0..$FFFC (the RESET vector at $FFFE is calculated)
var Turbo9osVectors = []uint16{
	0x0000, 0x0100, 0x0103, 0x010F, 0x010C, 0x0106, 0x0109,
}

// PrepareMemoryImage parses command line args:
// Either:
//   - exactly one .img file (65536 bytes)
//   - OR one or more OS-9 module files (synthesizing a 64KB image)
// Rejects if both are provided, or if no files provided.
func PrepareMemoryImage(files []string) ([]byte, error) {
	var imgFiles []string
	var modFiles []string

	for _, f := range files {
		if strings.HasSuffix(f, ".img") {
			imgFiles = append(imgFiles, f)
		} else {
			modFiles = append(modFiles, f)
		}
	}

	if len(imgFiles) > 0 && len(modFiles) > 0 {
		return nil, fmt.Errorf("cannot accept both .img file and OS-9 module files")
	}

	if len(imgFiles) == 1 {
		data, err := os.ReadFile(imgFiles[0])
		if err != nil {
			return nil, fmt.Errorf("cannot read image file %q: %w", imgFiles[0], err)
		}
		if len(data) != 65536 {
			return nil, fmt.Errorf("image file %q must be exactly 65536 bytes, got %d", imgFiles[0], len(data))
		}
		return data, nil
	} else if len(imgFiles) > 1 {
		return nil, fmt.Errorf("cannot accept more than one .img file (got %d)", len(imgFiles))
	}

	if len(modFiles) == 0 {
		return nil, fmt.Errorf("no image file or OS-9 module files provided")
	}

	// Synthesize 64KB image from OS-9 module files
	var combinedMods []byte
	for _, mf := range modFiles {
		bb, err := os.ReadFile(mf)
		if err != nil {
			return nil, fmt.Errorf("cannot read module file %q: %w", mf, err)
		}
		combinedMods = append(combinedMods, bb...)
	}

	if len(combinedMods) < 9 {
		return nil, fmt.Errorf("combined OS-9 modules too short (%d bytes)", len(combinedMods))
	}

	// Check OS-9 module sync bytes ($87, $CD)
	if combinedMods[0] != 0x87 || combinedMods[1] != 0xCD {
		return nil, fmt.Errorf("first module does not begin with OS-9 sync bytes ($87 $CD), got $%02X $%02X",
			combinedMods[0], combinedMods[1])
	}

	ramImage := make([]byte, 65536)

	// Copy ROM modules ending just before $FF00
	modLen := len(combinedMods)
	if modLen > 0xFF00 {
		return nil, fmt.Errorf("OS-9 modules too large (%d bytes exceeds $FF00)", modLen)
	}
	beginAddr := 0xFF00 - modLen
	copy(ramImage[beginAddr:0xFF00], combinedMods)

	// Install standard OS-9 vectors at $FFF0..$FFFD
	for i, vec := range Turbo9osVectors {
		addr := 0xFFF0 + i*2
		binary.BigEndian.PutUint16(ramImage[addr:addr+2], vec)
	}

	// Reset vector at $FFFE points to (beginAddr + kernel entry offset at offset 9)
	kernelEntryOffset := binary.BigEndian.Uint16(combinedMods[9:11])
	resetVector := uint16(beginAddr) + kernelEntryOffset
	binary.BigEndian.PutUint16(ramImage[0xFFFE:0x10000], resetVector)

	return ramImage, nil
}
