//XXX//go:build coco1
//XXX// +build coco1

package main

import (
	"bytes"
	"fmt"
)

type Coco1Ram struct {
	trackRam [COCO1_RAM_SIZE]byte
}

func (o *Coco1Ram) GetTrackRam() []byte {
	return o.trackRam[:]
}

type Rammer interface {
	Who() string
	CurrentMapString() string
	PPeek2(addr uint) uint
	Peek2(addr uint) uint
	PPeek1(addr uint) byte
	Peek1(addr uint) byte
	GetTrackRam() []byte
	Peek1WithMapping(addr uint, m Mapping) byte
	Peek2WithMapping(addr uint, m Mapping) uint
	MapAddrWithMapping(logical uint, m Mapping) uint
	LPeek1(addr uint) byte
	LPeek2(addr uint) uint
	Physical(addr uint) uint
	RamSize() uint
	RamMask() uint
	IoPhys() uint
	Poke1(addr uint, data byte)
}

var ram Rammer

var c1r = new(Coco1Ram)

func (o *Coco1Ram) Physical(addr uint) uint { return addr }

const COCO1_RAM_SIZE = 64 * 1024 // 64K
const COCO1_RAM_MASK = COCO1_RAM_SIZE - 1
const COCO1_IO_PHYS = COCO1_RAM_SIZE - 256

func (o *Coco1Ram) RamSize() uint { return COCO1_RAM_SIZE }
func (o *Coco1Ram) RamMask() uint { return COCO1_RAM_MASK }
func (o *Coco1Ram) IoPhys() uint  { return COCO1_IO_PHYS }

func (o *Coco1Ram) MapAddrWithMapping(logical uint, m Mapping) uint {
	return logical
}
func (o *Coco1Ram) Peek1WithMapping(addr uint, m Mapping) byte {
	return o.Peek1(addr)
}
func (o *Coco1Ram) Peek2WithMapping(addr uint, m Mapping) uint {
	return o.Peek2(addr)
}

func (o *Coco1Ram) DumpRam() {
	serial := MintSerial()
	Logf("DumpRam_%d (((((", serial)
	for i := uint(0); i < COCO1_RAM_SIZE; i += 16 {
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
func (o *Coco1Ram) PPeek1(addr uint) byte {
	return o.trackRam[addr&COCO1_RAM_MASK]
}
func (o *Coco1Ram) PPeek2(addr uint) uint {
	hi := o.PPeek1(addr)
	lo := o.PPeek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}

func (o *Coco1Ram) Peek1(addr uint) byte {
	return o.trackRam[addr&COCO1_RAM_MASK]
}
func (o *Coco1Ram) Peek2(addr uint) uint {
	hi := o.PPeek1(addr)
	lo := o.PPeek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}
func (o *Coco1Ram) LPeek1(addr uint) byte {
	return o.Peek1(addr)
}
func (o *Coco1Ram) LPeek2(addr uint) uint {
	return o.Peek2(addr)
}

func (o *Coco1Ram) Poke1(addr uint, data byte) {
	o.trackRam[addr&COCO1_RAM_MASK] = data
}

func (o *Coco1Ram) Who() string {
	dProc := o.Peek2(L1_D_Proc)
	if dProc == 0 {
		return ""
	}
	procID := o.Peek1(dProc + L1_P_ID)
	return Format("<%d>", procID)
}

func (o *Coco1Ram) CurrentMapString() string { return "" }
