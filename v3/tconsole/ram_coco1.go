//go:build coco1
// +build coco1

package main

import (
	"bytes"
	"fmt"
)

func CurrentHardwareMMap() string { return "" }

func Physical(addr uint) uint { return addr }

const RAM_SIZE = 64 * 1024 // 64K
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

func Peek1(addr uint) byte {
	return trackRam[addr&RAM_MASK]
}
func Peek2(addr uint) uint {
	hi := PPeek1(addr)
	lo := PPeek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}
func LPeek1(addr uint) byte {
	return Peek1(addr)
}
func LPeek2(addr uint) uint {
	return Peek2(addr)
}

func Who() string {
	dProc := Peek2(D_Proc)
	if dProc == 0 {
		return ""
	}
	procID := Peek1(dProc + P_ID)
	return Format("<%d>", procID)
}

func CurrentMapString() string { return "" }
