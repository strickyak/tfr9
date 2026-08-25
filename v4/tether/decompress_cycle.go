package main

import (
// "log"
)

// decompress_cycle.go — Decompressor for the bus spy tether compression protocol.
//
// Mirrors firmware-pio/compress.h (C compressor on RP2350).
// Call DecompressCycles(buf) to reconstruct a slice of 32-bit bus cycle words.
//
// Each reconstructed word has the form: [FIFO_type(8) | abus(16) | dbus(8)]
// Old-read dbus values are set to 0; the caller should substitute the known
// ROM byte for that address if the full dbus value is needed.

const (
	dcFifoRead  = uint32(0x01000000)
	dcFifoWrite = uint32(0x03000000)
)

// Package-level decompressor state, shared across calls for better compression.
// Call ResetDecompressCycles() to start fresh for a new session.
var (
	dcOldReadCS cycleDecompressState
	dcNewReadCS cycleDecompressState
	dcWriteCS   cycleDecompressState
)

// ResetDecompressCycles resets all three cycle states to zero.
// Omit between consecutive compressed blocks to preserve
// cross-block compression context.
func ResetDecompressCycles() {
	dcOldReadCS = cycleDecompressState{}
	dcNewReadCS = cycleDecompressState{}
	dcWriteCS = cycleDecompressState{}
}

// ============================================================
// cycleDecompressState mirrors CycleCompressState in compress.h.
// ============================================================

type cycleDecompressState struct {
	prev    uint16
	zone16  [16]uint16
	page256 [256]uint16
}

// ============================================================
// bitReader: reads bits MSB-first from a byte slice.
// ============================================================

type bitReader struct {
	buf     []byte
	bytePos int
	bitPos  int // next bit to read: 7 = MSB, 0 = LSB
}

func newBitReader(buf []byte) *bitReader {
	return &bitReader{buf: buf, bytePos: 0, bitPos: 7}
}

// readBits reads the next n bits and returns (value, true).
// Returns (0, false) if the buffer is exhausted.
func (br *bitReader) readBits(n int) (uint32, bool) {
	var result uint32
	for i := n - 1; i >= 0; i-- {
		if br.bytePos >= len(br.buf) {
			return 0, false
		}
		bit := uint32(br.buf[br.bytePos]>>uint(br.bitPos)) & 1
		result |= bit << uint(i)
		br.bitPos--
		if br.bitPos < 0 {
			br.bitPos = 7
			br.bytePos++
		}
	}
	return result, true
}

// ============================================================
// Sign extension helpers.
// ============================================================

// signExtend3 sign-extends a 3-bit two's-complement value. Range: -4..+3.
func signExtend3(v int) int {
	if v >= 4 {
		return v - 8
	}
	return v
}

// signExtend6 sign-extends a 6-bit two's-complement value. Range: -32..+31.
func signExtend6(v int) int {
	if v >= 32 {
		return v - 64
	}
	return v
}

// ============================================================
// decodeZoneAddr: decode zone+page address encoding.
// Called after the 2-bit prev-delta "11" has already been consumed.
// Updates cs.zone16[zone] and cs.page256[page] as required.
// Sets cs.prev to the decoded address.
// ============================================================

func decodeZoneAddr(br *bitReader, cs *cycleDecompressState) (uint16, bool) {
	zoneBits, ok := br.readBits(4)
	if !ok {
		return 0, false
	}
	zone := int(zoneBits)

	// Discriminator: first bit determines zone delta encoding.
	firstBit, ok := br.readBits(1)
	if !ok {
		return 0, false
	}

	var abus uint16

	if firstBit == 1 {
		// 1ddd — 3-bit signed delta from zone16[zone]
		ddd, ok := br.readBits(3)
		if !ok {
			return 0, false
		}
		abus = uint16(int(cs.zone16[zone]) + signExtend3(int(ddd)))

	} else {
		// Starts with 0; read one more bit.
		secondBit, ok := br.readBits(1)
		if !ok {
			return 0, false
		}

		if secondBit == 1 {
			// 01dddddd — 6-bit signed delta from zone16[zone]
			dddddd, ok := br.readBits(6)
			if !ok {
				return 0, false
			}
			abus = uint16(int(cs.zone16[zone]) + signExtend6(int(dddddd)))

		} else {
			// 00pppp — page mode: next 4 bits are the low nibble of the page.
			pppp, ok := br.readBits(4)
			if !ok {
				return 0, false
			}
			page := (zone << 4) | int(pppp)

			// Page delta discriminator.
			pFirstBit, ok := br.readBits(1)
			if !ok {
				return 0, false
			}

			if pFirstBit == 1 {
				// 1ddd — 3-bit signed delta from page256[page]
				ddd, ok := br.readBits(3)
				if !ok {
					return 0, false
				}
				abus = uint16(int(cs.page256[page]) + signExtend3(int(ddd)))

			} else {
				pSecondBit, ok := br.readBits(1)
				if !ok {
					return 0, false
				}

				if pSecondBit == 1 {
					// 01dddddd — 6-bit signed delta from page256[page]
					dddddd, ok := br.readBits(6)
					if !ok {
						return 0, false
					}
					abus = uint16(int(cs.page256[page]) + signExtend6(int(dddddd)))

				} else {
					// 00aaaaaaaa — raw low 8 bits; top 8 bits come from page.
					low8, ok := br.readBits(8)
					if !ok {
						return 0, false
					}
					abus = uint16((page << 8) | int(low8))
				}
			}
			cs.page256[page] = abus
		}
	}

	cs.zone16[zone] = abus
	cs.prev = abus
	return abus, true
}

// ============================================================
// DecompressCycles — main entry point.
// ============================================================
//
// Decompresses a byte slice produced by CompressCycles (firmware-pio/compress.h).
// Returns a slice of reconstructed 32-bit bus cycle words.
//
// For old-read cycles, dbus is set to 0 because it is implicit (known by the
// receiver via the ROM image).  Callers should substitute the correct byte.
//
// Stops after decoding numCycles cycles, or at end of buffer.

func DecompressCycles(compressed []byte, numCycles int) []uint32 {
	// log.Printf("DecompressCycles: % 3x", compressed)
	br := newBitReader(compressed)

	var result []uint32

outer:
	for len(result) < numCycles {
		// Read 2-bit cycle type.
		cycleType, ok := br.readBits(2)
		if !ok {
			break // end of buffer — handles 2-bit "00" tail padding
		}

		if cycleType == 3 {
			// 11 = old read "next": address is old_read_cs.prev + 1, no further bits.
			dcOldReadCS.prev++
			dbus := the_ram.Peek1(uint(dcOldReadCS.prev))
			result = append(result, dcFifoRead|(uint32(dcOldReadCS.prev)<<8)|uint32(dbus))
			continue
		}

		// Identify cycle state and FIFO type.
		var cs *cycleDecompressState
		var fifoType uint32
		isOldRead := false
		switch cycleType {
		case 0: // 00 = old read
			cs = &dcOldReadCS
			fifoType = dcFifoRead
			isOldRead = true
		case 1: // 01 = new read
			cs = &dcNewReadCS
			fifoType = dcFifoRead
		case 2: // 10 = write
			cs = &dcWriteCS
			fifoType = dcFifoWrite
		}

		// Read 2-bit prev-delta.
		deltaBits, ok := br.readBits(2)
		if !ok {
			break
		}

		// Sentinel: old-read + incr (cycle "00", delta "10") is never
		// a valid encoding — it is always compressed as "11" — so its
		// presence here signals end-of-buffer padding.
		if isOldRead && deltaBits == 2 {
			break
		}

		// Decode address from delta.
		var abus uint16
		switch deltaBits {
		case 0: // 00 = prev - 1
			abus = cs.prev - 1
			cs.prev = abus
		case 1: // 01 = same
			abus = cs.prev
		case 2: // 10 = incr (only for new-read and write)
			abus = cs.prev + 1
			cs.prev = abus
		case 3: // 11 = zone encoding
			abus, ok = decodeZoneAddr(br, cs)
			if !ok {
				break outer // end of data mid-zone
			}
		}

		// Read dbus (8 flat bits) for new-read and write; old-read has none.
		var dbus byte
		if isOldRead {
			dbus = the_ram.Peek1(uint(abus))
		} else {
			d, ok := br.readBits(8)
			if !ok {
				break outer // end of data mid-dbus
			}
			dbus = byte(d)
		}

		result = append(result, fifoType|(uint32(abus)<<8)|uint32(dbus))
	}

	// log.Printf("DecompressCycles: => % 9x", result)
	return result
}
