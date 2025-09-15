package main

import (
	"bytes"
	"fmt"
	"strings"
)

type Os9Level2 struct {
}

func (o *Os9Level2) GetMappingFromTable(addr uint) Mapping {
	// Mappings are always in block 0?
	return Mapping{
		// TODO: drop the "0x3F &".
		0x3F & the_ram.PPeek2(addr),
		0x3F & the_ram.PPeek2(addr+2),
		0x3F & the_ram.PPeek2(addr+4),
		0x3F & the_ram.PPeek2(addr+6),
		0x3F & the_ram.PPeek2(addr+8),
		0x3F & the_ram.PPeek2(addr+10),
		0x3F & the_ram.PPeek2(addr+12),
		0x3F & the_ram.PPeek2(addr+14),
	}
}

const (
	l2stateNew = iota
	l2state_2xxx
	l2state_Exxx
	l2state_ModDir
)

var l2state int
var l2bootModules []*ScannedModuleInfo

const (
	KernelTablesBelowHere = 0x0400
	Track35BootAddress    = 0xED00
)

func (o *Os9Level2) MemoryModuleOf(phys uint) (name string, offset uint) {
	ram := the_ram.GetTrackRam()
	a16 := phys & 0xFFFF
	switch l2state {
	case l2stateNew:
		// println(11111, phys)
		// First scan, to find modules in low memory, starting at $2600
		l2bootModules = ScanRamForMemoryModules(ram)
		l2state = l2state_2xxx
		return
	case l2state_2xxx:
		if a16 > Track35BootAddress {
			// println(22222, phys, a16)
			// Second scan, to find them copied up to $ED00
			l2bootModules = ScanRamForMemoryModules(ram)
			l2state = l2state_Exxx
		}
		name, offset = SearchScannedModuleInfo(l2bootModules, phys, ram)
		return
	case l2state_Exxx:
		if KernelTablesBelowHere < a16 && a16 < Track35BootAddress {
			// println(33333, phys, a16)
			// Third scan, to find krnp2 and everything else.
			l2bootModules = ScanRamForMemoryModules(ram)
			l2state = l2state_ModDir
		}
		name, offset = SearchScannedModuleInfo(l2bootModules, phys, ram)
		return
	case l2state_ModDir:
		break
	default:
		Fatalf("should never happen")
	}

	name, offset = SearchScannedModuleInfo(l2bootModules, phys, ram)
	if name != "" {
		return // good, we found it
	}

	beginDir, endDir := the_ram.PPeek2(L2_D_ModDir), the_ram.PPeek2(L2_D_ModDir+2)
	datPtr0 := the_ram.PPeek2(beginDir)
	AssertGT(datPtr0, 0)

	if beginDir != 0 && endDir != 0 {
		for i := beginDir; i < endDir; i += 8 {
			datPtr := the_ram.PPeek2(i)
			if datPtr == 0 {
				continue
			}
			mapping := o.GetMappingFromTable(datPtr)

			begin := the_ram.PPeek2(i + 4)
			if begin == 0 {
				continue
			}

			magic := the_ram.Peek2WithMapping(begin, mapping)
			if magic != 0x87CD {
				return "=m=", phys
			}
			// Logf("DDT: magic i=%x datPtr=%x begin=%x mapping=% 03x", i, datPtr, begin, mapping)

			modSize := the_ram.Peek2WithMapping(begin+2, mapping)
			//modNamePtr := the_ram.Peek2WithMapping(begin+4, mapping)
			//_ = modNamePtr
			//links := the_ram.Peek2WithMapping(begin+6, mapping)

			remaining := modSize
			region := begin
			offset := uint(0)
			for remaining > 0 {
				// If module crosses paged blocks, it has more than one region.
				regionP := the_ram.MapAddrWithMapping(region, mapping)
				endOfRegionBlockP := 1 + (regionP | 0x1FFF)
				regionSize := remaining
				if regionSize > endOfRegionBlockP-regionP {
					// A smaller region of the module.
					regionSize = endOfRegionBlockP - regionP
				}

				// Logf("DDT: try regionP=%x (phys=%x) regionEnds=%x remain=%x", regionP, phys, regionP+regionSize, remaining)
				if regionP <= phys && phys < regionP+regionSize {
					//if links == 0 {
					// return "unlinkedMod", phys
					// log.Panicf("in unlinked module: i=%x phys=%x", i, phys)
					//}
					id := o.ModuleId(begin, mapping)
					delta := offset + (phys - regionP)
					// Logf("DDT: [links=%x] FOUND %q+%x", links, id, delta)
					return id, delta
				}
				remaining -= regionSize
				regionP += regionSize
				region += uint(regionSize)
				offset += uint(regionSize)
				// Logf("DDT: advanced remaining=%x regionSize=%x", remaining, regionSize)
			}

		}
	}
	return "==", phys
}

func (o *Os9Level2) ModuleId(begin uint, m Mapping) string {
	namePtr := begin + the_ram.Peek2WithMapping(begin+4, m)
	modname := strings.ToLower(o.Os9StringWithMapping(namePtr, m))
	sz := the_ram.Peek2WithMapping(begin+2, m)
	crc1 := the_ram.Peek1WithMapping(begin+sz-3, m)
	crc2 := the_ram.Peek1WithMapping(begin+sz-2, m)
	crc3 := the_ram.Peek1WithMapping(begin+sz-1, m)
	return fmt.Sprintf("%s.%04x%02x%02x%02x", modname, sz, crc1, crc2, crc3)
}

func (o *Os9Level2) Os9StringWithMapping(addr uint, m Mapping) string {
	// Logf("Os9StringWithMapping(%x, %v)", addr, m)
	// Logf("  ... %02x %02x %02x %02x",
	//    the_ram.Peek1WithMapping(addr, m),
	//    the_ram.Peek1WithMapping(addr+1, m),
	//    the_ram.Peek1WithMapping(addr+2, m),
	//    the_ram.Peek1WithMapping(addr+3, m))

	var buf bytes.Buffer
	for {
		var b byte = the_ram.Peek1WithMapping(addr, m)
		var ch byte = 0x7F & b
		if '!' <= ch && ch <= '~' {
			buf.WriteByte(ch)
		} else {
			break
		}
		if (b & 128) != 0 {
			break
		}
		addr++
	}
	return buf.String()
}
func (o *Os9Level2) Os9String(addr uint) string {
	var buf bytes.Buffer
	for {
		var b byte = the_ram.Peek1(addr)
		var ch byte = 0x7F & b
		if '!' <= ch && ch <= '~' {
			buf.WriteByte(ch)
		} else {
			break
		}
		if (b & 128) != 0 {
			break
		}
		addr++
	}
	return buf.String()
}

func (o *Os9Level2) HasMMap() bool { return true }
func (o *Os9Level2) CurrentHardwareMMap() string {
	init0, init1 := the_ram.PPeek1(0x3ff90), the_ram.PPeek1(0x3ff91)
	mmuEnable := (init0 & 0x40) != 0
	if !mmuEnable {
		return "No"
	}
	mmuTask := init1 & 1

	mapHW := uint(0x3FFA0) // Task 0 map hardware
	if mmuTask != 0 {
		mapHW += 8 // task 1 map hardware
	}

	return fmt.Sprintf("T%x(%x %x %x %x  %x %x %x %x)",
		mmuTask,
		the_ram.PPeek1(mapHW+0),
		the_ram.PPeek1(mapHW+1),
		the_ram.PPeek1(mapHW+2),
		the_ram.PPeek1(mapHW+3),
		the_ram.PPeek1(mapHW+4),
		the_ram.PPeek1(mapHW+5),
		the_ram.PPeek1(mapHW+6),
		the_ram.PPeek1(mapHW+7))
}

func (o *Os9Level2) FormatOs9Chars(vec []byte) string {
	var buf bytes.Buffer
	buf.WriteByte('|')
	for _, x := range vec {
		ch := 127 & x
		if ch < 32 || ch > 126 {
			ch = '.'
		}
		if ch == 0 {
			ch = '-'
		}
		buf.WriteByte(ch)
		if (128 & x) != 0 {
			buf.WriteByte('~')
		}
	}
	buf.WriteByte('|')
	return buf.String()
}

func (o *Os9Level2) FormatOs9StringFromRam(addr uint) string {
	a := addr
	var buf bytes.Buffer
	//buf.WriteByte('`')
	for {
		x := the_ram.LPeek1(a)
		if x == 0 || x == 10 || x == 13 {
			break
		}
		ch := 127 & x
		if ch < 32 || ch > 126 {
			ch = '?'
		}
		buf.WriteByte(ch)
		if (x & 128) != 0 {
			break
		}
		a++
		if a > addr+300 || a < addr {
			buf.WriteByte('?')
			buf.WriteByte('?')
			buf.WriteByte('?')
			break
		}
	}
	//buf.WriteByte('`')
	return buf.String()
}

func (o *Os9Level2) FormatReturn(os9num byte, call *Os9ApiCall, rec *EventRec) (string, *Regs) {
	p := rec.Datas
	cc, rb := p[0], p[2]
	regs := &Regs{
		cc: p[0],
		a:  p[1],
		b:  p[2],
		dp: p[3],
		d:  (uint(p[1]) << 8) | uint(p[2]),
		x:  (uint(p[4]) << 8) | uint(p[5]),
		y:  (uint(p[6]) << 8) | uint(p[7]),
		u:  (uint(p[8]) << 8) | uint(p[9]),
	}

	if (cc & 1) == 1 { // if Carry bit set (bit 0x01)
		errname, _ := Os9ApiErrorNames[rb]
		errdesc, _ := Os9ApiErrorDescription[rb]
		return Format("ERROR(=$%x=%d.=%s=) *** %s ***", rb, rb, errname, strings.TrimSpace(errdesc)), regs
	}

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "( ")
	if call == nil {
		fmt.Fprintf(&buf, "RD=%02x, RX=%02x, RY=%02x, RU=%02x", p[1:3], p[4:6], p[6:8], p[8:10])

	} else {
		if call.RA != "" {
			fmt.Fprintf(&buf, "RA=%s=%02x, ", call.RA, p[1])
		}
		if call.RB != "" {
			fmt.Fprintf(&buf, "RB=%s=%02x, ", call.RB, p[2])
		}
		if call.RD != "" {
			fmt.Fprintf(&buf, "RD=%s=%02x, ", call.RD, p[1:3])
		}
		if call.RX != "" {
			rx := 256*uint(p[4]) + uint(p[5])
			if call.X[0] == '$' {
				fmt.Fprintf(&buf, "RX=%s=%04x=%q, ", call.RX, rx, o.FormatOs9StringFromRam(rx))
			} else {
				fmt.Fprintf(&buf, "RX=%s=%04x, ", call.RX, rx)
			}
		}
		if call.RY != "" {
			fmt.Fprintf(&buf, "RY=%s=%02x, ", call.RY, p[6:8])
		}
		if call.RU != "" {
			fmt.Fprintf(&buf, "RU=%s=%02x, ", call.RU, p[8:10])
		}
	}
	fmt.Fprintf(&buf, ")")
	return buf.String(), regs
}

func (o *Os9Level2) FormatCall(os9num byte, call *Os9ApiCall, rec *EventRec) (string, *Regs) {
	var buf bytes.Buffer
	a := rec.Datas[2:]
	var p [12]byte
	for i := 0; i < 12; i++ {
		// Logf("p[%d] = a[%d] = %02x\n", i,  11 - i, a[ 11 - i])
		p[i] = a[11-i]
	}
	Logf("% 3x\n", p[:])

	regs := &Regs{
		cc: p[0],
		a:  p[1],
		b:  p[2],
		dp: p[3],
		d:  (uint(p[1]) << 8) | uint(p[2]),
		x:  (uint(p[4]) << 8) | uint(p[5]),
		y:  (uint(p[6]) << 8) | uint(p[7]),
		u:  (uint(p[8]) << 8) | uint(p[9]),
	}

	if call == nil {
		fmt.Fprintf(&buf, "$%02x = UNKNOWN ( D=%02x, X=%02x, Y=%02x, U=%02x, ", os9num, p[1:3], p[4:6], p[6:8], p[8:10])

	} else {
		fmt.Fprintf(&buf, "$%02x = %s ( ", os9num, call.Name)
		if call.A != "" {
			fmt.Fprintf(&buf, "A=%s=%02x, ", call.A, p[1])
		}
		if call.B != "" {
			if call.B == "errnum" {
				errname, _ := Os9ApiErrorNames[p[2]]
				fmt.Fprintf(&buf, "B=%s=%02x=%q, ", call.B, p[2], errname)
			} else {
				fmt.Fprintf(&buf, "B=%s=%02x, ", call.B, p[2])
			}
		}
		if call.D != "" {
			fmt.Fprintf(&buf, "D=%s=%02x, ", call.D, p[1:3])
		}
		if call.X != "" {
			x := 256*uint(p[4]) + uint(p[5])
			if call.X[0] == '$' {
				fmt.Fprintf(&buf, "X=%s=%04x=%q, ", call.X, x, o.FormatOs9StringFromRam(x))
			} else {
				fmt.Fprintf(&buf, "X=%s=%02x, ", call.X, x)
			}
		}
		if call.Y != "" {
			fmt.Fprintf(&buf, "Y=%s=%02x, ", call.Y, p[6:8])
		}
		if call.U != "" {
			fmt.Fprintf(&buf, "U=%s=%02x, ", call.U, p[8:10])
		}
	}

	fmt.Fprintf(&buf, ")")
	return buf.String(), regs
}
