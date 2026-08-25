// Sample label: "p=1,b=centipede,v=32z,mhz=250,seed=4c6b0747-2641-41ce-9b1a-0ea6251199d0,h=pizga.net,name=Sparkat,id=z50"
// Sample: go run gen_metadata_uf2.go -label 'p=1,b=tfr9,v=911h,mhz=250,h=pizga.net,id=uks,seed=6416156c-5f7a-11f1-bab5-c7f324d6838f'

package main

import (
	"encoding/binary"
	"fmt"
	"log"
	"os"
)

const (
	UF2_MAGIC_START0 = 0x0A324655
	UF2_MAGIC_START1 = 0x9E5D5157
	UF2_MAGIC_END    = 0x0AB16F30

	// RP2350 ARM Family ID
	// RP2350_ARM_FAMILY_ID = 0xe48bff56 // RP2350 ARM (Non-Secure)
	RP2350_ARM_FAMILY_ID = 0xe48bff59 // RP2350 ARM (Secure)

	// UF2 Flags: 0x00002000 means FamilyID is present
	UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000

	// Target address for the last page of a 16MB Flash
	// LABEL_TARGET_ADDR = 0x10FFFF00
	LABEL_TARGET_ADDR = 0x10FFF000

	LABEL_BLOCK_SIZE   = 512
	LABEL_PAYLOAD_SIZE = 256
)

func MakeUf2Label(label string, uf2_filename string) {
	if len(label) > LABEL_PAYLOAD_SIZE {
		log.Fatalf("Error: Label is %d bytes, max is %d", len(label), LABEL_PAYLOAD_SIZE)
	}

	// Create the 512-byte buffer for a single UF2 block
	block := make([]byte, LABEL_BLOCK_SIZE)

	// 1. Start Magics
	binary.LittleEndian.PutUint32(block[0:4], UF2_MAGIC_START0)
	binary.LittleEndian.PutUint32(block[4:8], UF2_MAGIC_START1)

	// 2. Flags
	binary.LittleEndian.PutUint32(block[8:12], UF2_FLAG_FAMILY_ID_PRESENT)

	// 3. Target Address
	binary.LittleEndian.PutUint32(block[12:16], LABEL_TARGET_ADDR)

	// 4. Payload Size (Fixed at 256 for Pico)
	binary.LittleEndian.PutUint32(block[16:20], LABEL_PAYLOAD_SIZE)

	// 5. Block Number (0 for a single-block file)
	binary.LittleEndian.PutUint32(block[20:24], 0)

	// 6. Number of Blocks (1 total)
	binary.LittleEndian.PutUint32(block[24:28], 1)

	// 7. Family ID (RP2350 ARM)
	binary.LittleEndian.PutUint32(block[28:32], RP2350_ARM_FAMILY_ID)

	// 8. Data Payload (Starts at offset 32)
	copy(block[32:32+len(label)], label)
	// Remaining bytes of the 256-byte payload are already 0 from make()

	// 9. End Magic (Starts at the very end of the 512-byte block)
	binary.LittleEndian.PutUint32(block[508:512], UF2_MAGIC_END)

	// Write to file
	err := os.WriteFile(uf2_filename, block, 0644)
	if err != nil {
		log.Fatalf("Failed to write file: %v", err)
	}

	fmt.Printf("Successfully generated %q\n", uf2_filename)
	fmt.Printf("Target Address: 0x%08X\n", LABEL_TARGET_ADDR)
	fmt.Printf("Label: %q\n", label)
}
