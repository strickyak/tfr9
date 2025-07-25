//XXX//go:build coco3
//XXX// +build coco3

package main

import (
	"bytes"
	"fmt"
)

type Coco3Ram struct {
	trackRam [COCO3_RAM_SIZE]byte
}

var c3r = new(Coco3Ram)

type Mapping [8]uint

const COCO3_RAM_SIZE = 128 * 1024 // 128K
const COCO3_RAM_MASK = COCO3_RAM_SIZE - 1
const COCO3_IO_PHYS = COCO3_RAM_SIZE - 256

func (o *Coco3Ram) RamSize() uint { return COCO3_RAM_SIZE }
func (o *Coco3Ram) RamMask() uint { return COCO3_RAM_MASK }
func (o *Coco3Ram) IoPhys() uint  { return COCO3_IO_PHYS }

func (o *Coco3Ram) DumpRam() {
	serial := MintSerial()
	Logf("DumpRam_%d (((((", serial)
	for i := uint(0); i < COCO3_RAM_SIZE; i += 16 {
		dirty := false
		for j := uint(i); j < i+16; j++ {
			if o.trackRam[j] != 0 {
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
			fmt.Fprintf(&buf, "%02x ", o.trackRam[j])
		}
		buf.WriteByte(' ')
		buf.WriteByte('|')
		for j := uint(i); j < i+16; j++ {
			x := o.trackRam[j]
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
func (o *Coco3Ram) PPeek1(addr uint) byte {
	return o.trackRam[addr&COCO3_RAM_MASK]
}
func (o *Coco3Ram) PPeek2(addr uint) uint {
	hi := o.PPeek1(addr)
	lo := o.PPeek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}

func (o *Coco3Ram) Poke1(addr uint, data byte) {
	longaddr := o.Physical(addr)
	o.trackRam[longaddr&COCO3_RAM_MASK] = data
}

func (o *Coco3Ram) GetTrackRam() []byte {
	return o.trackRam[:]
}

func (o *Coco3Ram) Peek1(addr uint) byte {
	longaddr := o.Physical(addr)
	return o.trackRam[longaddr&COCO3_RAM_MASK]
}
func (o *Coco3Ram) Peek2(addr uint) uint {
	hi := o.PPeek1(addr)
	lo := o.PPeek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}

func (o *Coco3Ram) LPeek1(addr uint) byte {
	return o.Peek1(addr)
}
func (o *Coco3Ram) LPeek2(addr uint) uint {
	return o.Peek2(addr)
}

// SECOND IMPLEMENTATION

func (o *Coco3Ram) PhysicalAddrAtBlock(addr uint, block uint) uint {
	addr &= 0x1FFF        // Use bottom 13 bits of address.
	region := block << 13 // block provides the higher bits.
	z := (addr | region) & COCO3_RAM_MASK
	//Logf("PhysicalAddrAtBlock: %04x %x -> %04x", addr, block, z)
	return z
}
func (o *Coco3Ram) Peek1AtBlock(addr uint, block uint) byte {
	p := o.PhysicalAddrAtBlock(addr, block)
	z := o.trackRam[p]
	//Logf("Peek1AtBlock: %04x %x -> %04x %02x", addr, block, p, z)
	return z
}

func (o *Coco3Ram) PhysicalAddrWithMmu(logical uint) uint {
	AssertLT(logical, 0x10000)

	const DefaultBlock = 0x38
	const IOBlock = 0x3F
	const MmuEnableAddr = 0xFF90
	const MmuEnableBit = 0x40
	const FExxEnableBit = 0x40
	const MmuTaskAddr = 0xFF91
	const MmuTaskBit = 0x01

	slot := logical >> 13
	offset := logical & 0x1FFF

	e := o.Peek1AtBlock(MmuEnableAddr, IOBlock)
	enabled := (e & MmuEnableBit) != 0
	if !enabled {
		z := o.PhysicalAddrAtBlock(offset, DefaultBlock+slot)
		//Logf("PhysicalAddrWithMmu(%04x) => No %06x", uint, z)
		return z
	}

	if 0xFF00 <= logical {
		z := o.PhysicalAddrAtBlock(offset, DefaultBlock+slot)
		//Logf("PhysicalAddrWithMmu(%04x) => (FF) %06x", z)
		return z
	}

	fexx := (e & FExxEnableBit) != 0
	if fexx && 0xFE00 <= logical {
		z := o.PhysicalAddrAtBlock(offset, DefaultBlock+slot)
		//Logf("PhysicalAddrWithMmu(%04x) => (FE) %06x", z)
		return z
	}

	task := uint(o.Peek1AtBlock(MmuTaskAddr, IOBlock) & MmuTaskBit)
	mapbyte := o.Peek1AtBlock(0xFFA0+slot+8*task, IOBlock)
	z := o.PhysicalAddrAtBlock(offset, uint(mapbyte))
	//Logf("PhysicalAddrWithMmu(%04x) => %x %x %06x", task, mapbyte, z)
	return z
}

func (o *Coco3Ram) Who() string {
	init1 := o.PPeek1(0x3ff91)
	mmuTask := init1 & 1
	if mmuTask == 0 {
		return ""
	}
	dProc := o.Peek2WithHalf(L2_D_Proc, 0)
	if dProc == 0 {
		return ""
	}
	procID := o.Peek1WithHalf(dProc+L2_P_ID, 0)
	pModul := o.Peek2WithHalf(dProc+L2_P_PModul, 0)
	mName := o.Peek2WithHalf(pModul+L2_M_Name, 1)
	s := pModul + mName

	// Logf("dP=%x pM=%x mN=%x s=%x", dProc, pModul, mName, s)

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "%x:", procID)
	for {
		ch := o.Peek1WithHalf(s, 1)
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

func (o *Coco3Ram) CurrentMapString() string {
	const DefaultBlock = 0x38
	const IOBlock = 0x3F
	const MmuEnableAddr = 0xFF90
	const MmuEnableBit = 0x40
	const FExxEnableBit = 0x40
	const MmuTaskAddr = 0xFF91
	const MmuTaskBit = 0x01

	e := o.Peek1AtBlock(MmuEnableAddr, IOBlock)
	enabled := (e & MmuEnableBit) != 0

	if !enabled {
		return "No"
	}

	task := uint(o.Peek1AtBlock(MmuTaskAddr, IOBlock) & MmuTaskBit)

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "T%x[", task)
	for a := uint(0); a < 8; a++ {
		fmt.Fprintf(&buf, "%x ", o.LPeek1(a+0xFFA0+8*task))
	}
	fmt.Fprintf(&buf, "]")

	return buf.String()
}

func (o *Coco3Ram) Peek1WithMapping(addr uint, m Mapping) byte {
	logBlock := (addr >> 13) & 7
	physBlock := m[logBlock]
	if addr >= 0xFF00 {
		physBlock = 0x3F
	} else if addr >= 0xFE00 { // TODO: check FExx bit.
		physBlock = 0x3F
	}
	ptr := (addr & 0x1FFF) | (uint(physBlock) << 13)
	return o.PPeek1(ptr)
}
func (o *Coco3Ram) Peek2WithMapping(addr uint, m Mapping) uint {
	hi := o.Peek1WithMapping(addr, m)
	lo := o.Peek1WithMapping(addr+1, m)
	return (uint(hi) << 8) | uint(lo)
}
func (o *Coco3Ram) MapAddrWithMapping(logical uint, m Mapping) uint {
	slot := 7 & (logical >> 13)
	low13 := uint(logical & 0x1FFF)
	physicalPage := m[slot]
	return (uint(physicalPage) << 13) | low13
}

func (o *Coco3Ram) HalfNumberToMapping(half byte) Mapping {
	base := COCO3_IO_PHYS + 0xA0 + (8 * uint(half))
	var m Mapping
	for i := uint(0); i < 8; i++ {
		m[i] = uint(o.PPeek1(base + i))
	}
	// Log("Half %x %x => %x", half, base, m)
	return m
}
func (o *Coco3Ram) Peek1WithHalf(addr uint, half byte) byte {
	m := o.HalfNumberToMapping(half)
	return o.Peek1WithMapping(addr, m)
}
func (o *Coco3Ram) Peek2WithHalf(addr uint, half byte) uint {
	m := o.HalfNumberToMapping(half)
	return o.Peek2WithMapping(addr, m)
}
func (o *Coco3Ram) Physical(logical uint) uint {
	return o.PhysicalAddrWithMmu(logical)
	/*
		block := byte(0x3F) // tentative
		low13 := uint(logical & 0x1FFF)

		if logical < 0xFE00 { // must compute block
			mapHW := uint(0x3FFA0) // Task 0 map hardware
			if (o.PPeek1(0x3FF91) & 1) != 0 {
				mapHW += 8 // task 1 map hardware
			}
			//log.Printf("mapHW: %x %x %x %x %x %x %x %x",
			//o.PPeek1(mapHW+0),
			//o.PPeek1(mapHW+1),
			//o.PPeek1(mapHW+2),
			//o.PPeek1(mapHW+3),
			//o.PPeek1(mapHW+4),
			//o.PPeek1(mapHW+5),
			//o.PPeek1(mapHW+6),
			//o.PPeek1(mapHW+7))

			slot := 7 & (logical >> 13)
			block = o.PPeek1(mapHW + slot)
			//log.Printf("Physical: a=%x b=%x p=%x", logical, block, (uint(block) << 13) | low13)
		}

		return (uint(block) << 13) | low13
	*/
}
