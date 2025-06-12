//go:build coco3
// +build coco3

package main

import (
	"bytes"
	"fmt"
)

type Mapping [8]uint

const RAM_SIZE = 128 * 1024 // 128K
const RAM_MASK = RAM_SIZE - 1
const IO_PHYS = RAM_SIZE - 256

var trackRam [RAM_SIZE]byte

func DumpRam() {
	serial := MintSerial()
	Logf("DumpRam_%d (((((", serial)
	for i := uint(0); i < RAM_SIZE; i += 16 {
		dirty := false
		for j := uint(i); j < i+16; j++ {
			if trackRam[j] != 0 {
				dirty = true
			}
		}
		if !dirty {
			continue
		}
		var buf bytes.Buffer
		fmt.Fprintf(&buf, ";%06x: ", i)
		for j := uint(i); j < i+16; j++ {
			if (j & 3) == 0 {
				buf.WriteByte(' ')
			}
			fmt.Fprintf(&buf, "%02x ", trackRam[j])
		}
		buf.WriteByte(' ')
		buf.WriteByte('|')
		for j := uint(i); j < i+16; j++ {
			x := trackRam[j]
			y := x & 0x7F
			if 32 <= y && y <= 126 {
				buf.WriteByte(y)
			} else if y == 0 {
				buf.WriteByte('-')
			} else {
				buf.WriteByte('.')
			}
			if 32 <= y && y <= 126 && (x&0x80) != 0 {
				buf.WriteByte('~') // post-byte
			}
		}
		buf.WriteByte('|')
		Logf(buf.String())
	}
	Logf("DumpRam_%d )))))", serial)
}

// PPeek1: Physical Memory Peek
func PPeek1(addr uint) byte {
	return trackRam[addr&RAM_MASK]
}
func PPeek2(addr uint) uint {
	hi := PPeek1(addr)
	lo := PPeek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}

// SECOND IMPLEMENTATION

func PhysicalAddrAtBlock(addr uint, block uint) uint {
	addr &= 0x1FFF        // Use bottom 13 bits of address.
	region := block << 13 // block provides the higher bits.
	z := (addr | region) & (RAM_SIZE - 1)
	//Logf("PhysicalAddrAtBlock: %04x %x -> %04x", addr, block, z)
	return z
}
func Peek1AtBlock(addr uint, block uint) byte {
	p := PhysicalAddrAtBlock(addr, block)
	z := trackRam[p]
	//Logf("Peek1AtBlock: %04x %x -> %04x %02x", addr, block, p, z)
	return z
}

func PhysicalAddrWithMmu(logical uint) uint {
	if (logical &^ 0xFFFF) != 0 {
		Panicf("bad logical: %x", logical)
	}

	const DefaultBlock = 0x38
	const IOBlock = 0x3F
	const MmuEnableAddr = 0xFF90
	const MmuEnableBit = 0x40
	const FExxEnableBit = 0x40
	const MmuTaskAddr = 0xFF91
	const MmuTaskBit = 0x01

	slot := logical >> 13
	offset := logical & 0x1FFF

	e := Peek1AtBlock(MmuEnableAddr, IOBlock)
	enabled := (e & MmuEnableBit) != 0
	if !enabled {
		z := PhysicalAddrAtBlock(offset, DefaultBlock+slot)
		//Logf("PhysicalAddrWithMmu(%04x) => No %06x", uint, z)
		return z
	}

	if 0xFF00 <= logical {
		z := PhysicalAddrAtBlock(offset, DefaultBlock+slot)
		//Logf("PhysicalAddrWithMmu(%04x) => (FF) %06x", z)
		return z
	}

	fexx := (e & FExxEnableBit) != 0
	if fexx && 0xFE00 <= logical {
		z := PhysicalAddrAtBlock(offset, DefaultBlock+slot)
		//Logf("PhysicalAddrWithMmu(%04x) => (FE) %06x", z)
		return z
	}

	task := uint(Peek1AtBlock(MmuTaskAddr, IOBlock) & MmuTaskBit)
	mapbyte := Peek1AtBlock(0xFFA0+slot+8*task, IOBlock)
	z := PhysicalAddrAtBlock(offset, uint(mapbyte))
	//Logf("PhysicalAddrWithMmu(%04x) => %x %x %06x", task, mapbyte, z)
	return z
}

func LPeek1(addr uint) byte {
	return trackRam[PhysicalAddrWithMmu(addr)]
}

func Who() string {
	init1 := PPeek1(0x3ff91)
	mmuTask := init1 & 1
	if mmuTask == 0 {
		return ""
	}
	dProc := Peek2WithHalf(D_Proc, 0)
	if dProc == 0 {
		return ""
	}
	procID := Peek1WithHalf(dProc+P_ID, 0)
	pModul := Peek2WithHalf(dProc+P_PModul, 0)
	mName := Peek2WithHalf(pModul+M_Name, 1)
	s := pModul + mName

	// Logf("dP=%x pM=%x mN=%x s=%x", dProc, pModul, mName, s)

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "%x:", procID)
	for {
		ch := Peek1WithHalf(s, 1)
		s++
		a := ch & 0x7f
		if '!' <= a && a <= '~' {
			buf.WriteByte(a)
		} else {
			buf.WriteByte('?')
		}
		if (ch & 0x80) != 0 {
			break
		}
		if buf.Len() >= 12 {
			break
		}
	}
	return buf.String()
}

func CurrentMapString() string {
	const DefaultBlock = 0x38
	const IOBlock = 0x3F
	const MmuEnableAddr = 0xFF90
	const MmuEnableBit = 0x40
	const FExxEnableBit = 0x40
	const MmuTaskAddr = 0xFF91
	const MmuTaskBit = 0x01

	e := Peek1AtBlock(MmuEnableAddr, IOBlock)
	enabled := (e & MmuEnableBit) != 0

	if !enabled {
		return "No"
	}

	task := uint(Peek1AtBlock(MmuTaskAddr, IOBlock) & MmuTaskBit)

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "T%x[", task)
	for a := uint(0); a < 8; a++ {
		fmt.Fprintf(&buf, "%x ", LPeek1(a+0xFFA0+8*task))
	}
	fmt.Fprintf(&buf, "]")

	return buf.String()
}

func Peek1WithMapping(addr uint, m Mapping) byte {
	logBlock := (addr >> 13) & 7
	physBlock := m[logBlock]
	if addr >= 0xFF00 {
		physBlock = 0x3F
	} else if addr >= 0xFE00 { // TODO: check FExx bit.
		physBlock = 0x3F
	}
	ptr := (addr & 0x1FFF) | (uint(physBlock) << 13)
	return PPeek1(ptr)
}
func Peek2WithMapping(addr uint, m Mapping) uint {
	hi := Peek1WithMapping(addr, m)
	lo := Peek1WithMapping(addr+1, m)
	return (uint(hi) << 8) | uint(lo)
}
func MapAddrWithMapping(logical uint, m Mapping) uint {
	slot := 7 & (logical >> 13)
	low13 := uint(logical & 0x1FFF)
	physicalPage := m[slot]
	return (uint(physicalPage) << 13) | low13
}

func HalfNumberToMapping(half byte) Mapping {
	base := IO_PHYS + 0xA0 + (8 * uint(half))
	var m Mapping
	for i := uint(0); i < 8; i++ {
		m[i] = uint(PPeek1(base + i))
	}
	// Log("Half %x %x => %x", half, base, m)
	return m
}
func Peek1WithHalf(addr uint, half byte) byte {
	m := HalfNumberToMapping(half)
	return Peek1WithMapping(addr, m)
}
func Peek2WithHalf(addr uint, half byte) uint {
	m := HalfNumberToMapping(half)
	return Peek2WithMapping(addr, m)
}
func Physical(logical uint) uint {
	block := byte(0x3F) // tentative
	low13 := uint(logical & 0x1FFF)

	if logical < 0xFFE0 { // must compute block
		mapHW := uint(0x3FFA0) // Task 0 map hardware
		if (PPeek1(0x3FF91) & 1) != 0 {
			mapHW += 8 // task 1 map hardware
		}
		//log.Printf("mapHW: %x %x %x %x %x %x %x %x",
		//PPeek1(mapHW+0),
		//PPeek1(mapHW+1),
		//PPeek1(mapHW+2),
		//PPeek1(mapHW+3),
		//PPeek1(mapHW+4),
		//PPeek1(mapHW+5),
		//PPeek1(mapHW+6),
		//PPeek1(mapHW+7))

		slot := 7 & (logical >> 13)
		block = PPeek1(mapHW + slot)
		//log.Printf("Physical: a=%x b=%x p=%x", logical, block, (uint(block) << 13) | low13)
	}

	return (uint(block) << 13) | low13
}
