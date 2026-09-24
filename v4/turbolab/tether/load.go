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
//   - OR exactly one .decb file (converted into 64KB image with RESET vector at $FFFE)
//   - OR one or more OS-9 module files (synthesizing a 64KB image)
// Rejects if mixed, or if no files provided.
func PrepareMemoryImage(files []string) ([]byte, error) {
	var imgFiles []string
	var decbFiles []string
	var modFiles []string

	for _, f := range files {
		low := strings.ToLower(f)
		if strings.HasSuffix(low, ".img") {
			imgFiles = append(imgFiles, f)
		} else if strings.HasSuffix(low, ".decb") {
			decbFiles = append(decbFiles, f)
		} else {
			modFiles = append(modFiles, f)
		}
	}

	var kinds int
	if len(imgFiles) > 0 {
		kinds++
	}
	if len(decbFiles) > 0 {
		kinds++
	}
	if len(modFiles) > 0 {
		kinds++
	}

	if kinds > 1 {
		return nil, fmt.Errorf("cannot mix .img, .decb, and OS-9 module files")
	}

	if kinds == 0 {
		return nil, fmt.Errorf("no image file, DECB file, or OS-9 module files provided")
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

	if len(decbFiles) == 1 {
		return LoadDecbFile(decbFiles[0])
	} else if len(decbFiles) > 1 {
		return nil, fmt.Errorf("cannot accept more than one .decb file (got %d)", len(decbFiles))
	}

	if len(modFiles) == 0 {
		return nil, fmt.Errorf("no image file, DECB file, or OS-9 module files provided")
	}

	// Read each module file and check if it contains a kernel/krn module.
	// In TurbOS / OS-9, the kernel must be at the lowest address of primordial ROM
	// because it sets D.MLIM to its base address and scans modules upwards to $FF00.
	type modFileData struct {
		filename  string
		data      []byte
		hasKernel bool
	}

	var allModData []modFileData
	var kernelFiles []modFileData
	var otherFiles []modFileData

	for _, mf := range modFiles {
		bb, err := os.ReadFile(mf)
		if err != nil {
			return nil, fmt.Errorf("cannot read module file %q: %w", mf, err)
		}
		mods := ScanImageForOs9Modules(bb)
		hasK := false
		for _, m := range mods {
			low := strings.ToLower(m.Name)
			if low == "kernel" || low == "krn" {
				hasK = true
				break
			}
		}
		mfd := modFileData{filename: mf, data: bb, hasKernel: hasK}
		allModData = append(allModData, mfd)
		if hasK {
			kernelFiles = append(kernelFiles, mfd)
		} else {
			otherFiles = append(otherFiles, mfd)
		}
	}

	var orderedMods []modFileData
	if len(kernelFiles) > 0 {
		if !allModData[0].hasKernel {
			fmt.Fprintf(os.Stderr, "Note: reordering modules so %s (containing kernel) is loaded first\n", kernelFiles[0].filename)
		}
		orderedMods = append(kernelFiles, otherFiles...)
	} else {
		orderedMods = allModData
	}

	// Synthesize 64KB image from OS-9 module files
	var combinedMods []byte
	for _, mfd := range orderedMods {
		combinedMods = append(combinedMods, mfd.data...)
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

	// Locate kernel or krn module to determine the reset vector.
	// If found, reset vector points to kernel's execution entry point.
	// Otherwise, fall back to the first module in the image.
	var resetVector uint16
	scanned := ScanImageForOs9Modules(ramImage)
	var kernelMod *ScannedModuleInfo
	for _, m := range scanned {
		low := strings.ToLower(m.Name)
		if low == "kernel" || low == "krn" {
			kernelMod = m
			break
		}
	}

	if kernelMod != nil {
		kernelEntryOffset := binary.BigEndian.Uint16(ramImage[kernelMod.BaseAddr+9 : kernelMod.BaseAddr+11])
		resetVector = kernelMod.BaseAddr + kernelEntryOffset
	} else {
		kernelEntryOffset := binary.BigEndian.Uint16(combinedMods[9:11])
		resetVector = uint16(beginAddr) + kernelEntryOffset
	}
	binary.BigEndian.PutUint16(ramImage[0xFFFE:0x10000], resetVector)

	return ramImage, nil
}

// ParseDecb parses raw DECB format data into a 65536-byte memory image.
// It verifies that:
// 1. data is at least 10 bytes long
// 2. data begins with $00
// 3. the fifth, fourth, and third bytes before the end are $FF, $00, $00
// 4. all $00 data blocks are well-formed and fit in 64KB RAM
// 5. the $FF execution block entry point is stored at $FFFE (RESET vector)
// It returns the 64KB RAM image and the entry point address.
func ParseDecb(data []byte) ([]byte, uint16, error) {
	n := len(data)
	if n < 10 {
		return nil, 0, fmt.Errorf("DECB data too short (%d bytes, minimum 10)", n)
	}
	if data[0] != 0x00 {
		return nil, 0, fmt.Errorf("DECB data does not begin with $00, got $%02X", data[0])
	}
	if data[n-5] != 0xFF || data[n-4] != 0x00 || data[n-3] != 0x00 {
		return nil, 0, fmt.Errorf("DECB trailer at offset %d must be $FF $00 $00, got $%02X $%02X $%02X",
			n-5, data[n-5], data[n-4], data[n-3])
	}

	ramImage := make([]byte, 65536)
	pos := 0
	var foundExec bool
	var entryPoint uint16

	for pos < n {
		tag := data[pos]
		switch tag {
		case 0x00:
			if pos+5 > n {
				return nil, 0, fmt.Errorf("truncated DECB data block header at offset %d", pos)
			}
			sz := binary.BigEndian.Uint16(data[pos+1 : pos+3])
			addr := binary.BigEndian.Uint16(data[pos+3 : pos+5])
			pos += 5

			if pos+int(sz) > n {
				return nil, 0, fmt.Errorf("truncated DECB data payload at offset %d: expected %d bytes, got %d",
					pos, sz, n-pos)
			}
			if int(addr)+int(sz) > 65536 {
				return nil, 0, fmt.Errorf("DECB data block at offset %d exceeds 64KB RAM (addr $%04X + size $%04X = $%05X)",
					pos-5, addr, sz, int(addr)+int(sz))
			}
			copy(ramImage[addr:int(addr)+int(sz)], data[pos:pos+int(sz)])
			pos += int(sz)

		case 0xFF:
			if pos+5 > n {
				return nil, 0, fmt.Errorf("truncated DECB exec block header at offset %d", pos)
			}
			entryPoint = binary.BigEndian.Uint16(data[pos+3 : pos+5])
			pos += 5
			foundExec = true

			if pos < n {
				return nil, 0, fmt.Errorf("unexpected trailing data after DECB exec block at offset %d (%d bytes remaining)",
					pos, n-pos)
			}

		default:
			return nil, 0, fmt.Errorf("unknown DECB block type $%02X at offset %d", tag, pos)
		}
	}

	if !foundExec {
		return nil, 0, fmt.Errorf("DECB file missing execution block ($FF)")
	}

	binary.BigEndian.PutUint16(ramImage[0xFFFE:0x10000], entryPoint)
	return ramImage, entryPoint, nil
}

// LoadDecbFile reads a .decb file and converts it into a 65536-byte memory image.
func LoadDecbFile(filename string) ([]byte, error) {
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, fmt.Errorf("cannot read DECB file %q: %w", filename, err)
	}
	ramImage, entryPoint, err := ParseDecb(data)
	if err != nil {
		return nil, fmt.Errorf("invalid DECB file %q: %w", filename, err)
	}
	fmt.Fprintf(os.Stderr, "Loaded DECB file %q: entry point at $%04X (RESET vector set at $FFFE)\n", filename, entryPoint)
	return ramImage, nil
}
