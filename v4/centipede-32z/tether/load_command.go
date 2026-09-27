package main

import (
	"fmt"
	"os"
	"strings"
)

func init() {
	RegisterUCommand("load", handleLoadCommand)
}

func handleLoadCommand(args string) {
	err := ExecuteLoadCommand(args)
	if err != nil {
		fmt.Printf("\r*** %v\r\n", err)
	}
}

// ExecuteLoadCommand executes the ~load command to load a BASIC program into RAM.
// It verifies that no program is already in RAM (requiring NEW beforehand),
// compiles the BASIC file (pure ASCII or tokenized), uploads it to the Pico's RAM
// at TXTTAB, and updates all relevant system variables.
func ExecuteLoadCommand(args string) error {
	filename := strings.TrimSpace(args)
	if filename == "" {
		return fmt.Errorf("Usage: ~load <filename.bas>")
	}

	// If file doesn't exist as given, try appending .bas
	if _, err := os.Stat(filename); err != nil {
		if _, err2 := os.Stat(filename + ".bas"); err2 == nil {
			filename = filename + ".bas"
		} else {
			return fmt.Errorf("File error: %v", err)
		}
	}

	ch := GetChannelToPico()
	if ch == nil {
		return fmt.Errorf("Error: not connected to Pico")
	}

	if the_ram == nil {
		return fmt.Errorf("Error: the_ram is nil")
	}

	// Read zero-page pointers from Pico to determine TXTTAB and MEMSIZ.
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
	if txttab == 0 {
		return fmt.Errorf("Error: TXTTAB is 0; BASIC does not appear to be running")
	}

	// MEMSIZ is at $0027-$0028 (offset 23 in zpData: 0x27 - 0x10 = 23)
	memsiz := (uint(zpData[0x27-0x10]) << 8) | uint(zpData[0x28-0x10])

	// Check if a program is already in RAM.
	// When NEW has run, BASIC writes 0x00, 0x00 at TXTTAB.
	// If the 2-byte link pointer at TXTTAB is non-zero, a program is present.
	txtPhys := int(the_ram.Physical(txttab))
	firstWord, err := fetchRamRange(ch, txtPhys, 2)
	if err != nil {
		return fmt.Errorf("Error reading TXTTAB from Pico: %v", err)
	}
	if len(firstWord) < 2 {
		return fmt.Errorf("Error: short read of TXTTAB link pointer")
	}

	if firstWord[0] != 0 || firstWord[1] != 0 {
		return fmt.Errorf("Program already in RAM. Please type NEW before ~load")
	}

	// Load and compile BASIC file (ASCII or tokenized) for target TXTTAB
	programBytes, newEnd, numLines, err := LoadBasicFile(filename, txttab)
	if err != nil {
		return fmt.Errorf("Load error: %v", err)
	}

	if memsiz > 0 && newEnd >= memsiz {
		return fmt.Errorf("Error: program too large (ends at 0x%04X, MEMSIZ is 0x%04X)", newEnd, memsiz)
	}

	// Ensure preceding byte at TXTTAB-1 is 0x00
	precPhys := int(the_ram.Physical(txttab - 1))
	if err := WriteRamRange(ch, precPhys, []byte{0x00}); err != nil {
		return fmt.Errorf("Error writing preceding null byte: %v", err)
	}
	the_ram.Poke1(txttab-1, 0x00)

	// Write compiled program to Pico RAM
	if err := WriteRamRange(ch, txtPhys, programBytes); err != nil {
		return fmt.Errorf("Error writing program to RAM: %v", err)
	}
	for i, b := range programBytes {
		the_ram.Poke1(txttab+uint(i), b)
	}

	// Update zero-page system variables in Pico RAM and mirror in the_ram:
	// VARTAB ($001B-$001C) = newEnd
	// ARYTAB ($001D-$001E) = newEnd
	// ARYEND ($001F-$0020) = newEnd
	zp1 := []byte{
		byte(newEnd >> 8), byte(newEnd & 0xFF),
		byte(newEnd >> 8), byte(newEnd & 0xFF),
		byte(newEnd >> 8), byte(newEnd & 0xFF),
	}
	if err := WriteRamRange(ch, int(the_ram.Physical(0x001B)), zp1); err != nil {
		return fmt.Errorf("Error updating VARTAB/ARYTAB: %v", err)
	}
	the_ram.Poke1(0x001B, zp1[0])
	the_ram.Poke1(0x001C, zp1[1])
	the_ram.Poke1(0x001D, zp1[2])
	the_ram.Poke1(0x001E, zp1[3])
	the_ram.Poke1(0x001F, zp1[4])
	the_ram.Poke1(0x0020, zp1[5])

	// STRTAB ($0023-$0024) = memsiz
	if memsiz > 0 {
		zpStr := []byte{byte(memsiz >> 8), byte(memsiz & 0xFF)}
		if err := WriteRamRange(ch, int(the_ram.Physical(0x0023)), zpStr); err != nil {
			return fmt.Errorf("Error updating STRTAB: %v", err)
		}
		the_ram.Poke1(0x0023, zpStr[0])
		the_ram.Poke1(0x0024, zpStr[1])
	}

	// OLDPTR ($002D-$002E) = 0x0000 (can't CONT)
	zpOld := []byte{0x00, 0x00}
	if err := WriteRamRange(ch, int(the_ram.Physical(0x002D)), zpOld); err != nil {
		return fmt.Errorf("Error updating OLDPTR: %v", err)
	}
	the_ram.Poke1(0x002D, 0x00)
	the_ram.Poke1(0x002E, 0x00)

	// DATPTR ($0033-$0034) = txttab - 1
	datPtr := txttab - 1
	zpDat := []byte{byte(datPtr >> 8), byte(datPtr & 0xFF)}
	if err := WriteRamRange(ch, int(the_ram.Physical(0x0033)), zpDat); err != nil {
		return fmt.Errorf("Error updating DATPTR: %v", err)
	}
	the_ram.Poke1(0x0033, zpDat[0])
	the_ram.Poke1(0x0034, zpDat[1])

	// CHARAD ($00A6-$00A7) = txttab - 1
	zpChar := []byte{byte(datPtr >> 8), byte(datPtr & 0xFF)}
	if err := WriteRamRange(ch, int(the_ram.Physical(0x00A6)), zpChar); err != nil {
		return fmt.Errorf("Error updating CHARAD: %v", err)
	}
	the_ram.Poke1(0x00A6, zpChar[0])
	the_ram.Poke1(0x00A7, zpChar[1])

	// ARYDIS ($0008) = 0
	if err := WriteRamRange(ch, int(the_ram.Physical(0x0008)), []byte{0x00}); err != nil {
		return fmt.Errorf("Error updating ARYDIS: %v", err)
	}
	the_ram.Poke1(0x0008, 0x00)

	fmt.Printf("\r*** Loaded %d lines (%d bytes) from %s into RAM at 0x%04X (VARTAB=0x%04X)\r\n",
		numLines, len(programBytes), filename, txttab, newEnd)
	return nil
}
