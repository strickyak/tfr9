//go:build level1
// +build level1

package main

import (
	"bytes"
	"fmt"
	"strings"
)

type ScannedModuleInfo struct {
	Name     string
	Addy     uint
	Size     uint
	FullName string
}

func byt(i uint, ram []byte) byte {
	if i >= uint(len(ram)) {
		return 0
	}
	return ram[i]
}

func wrd(i uint, ram []byte) uint {
	return (uint(byt(i, ram)) << 8) | uint(byt(i+1, ram))
}

func ScanRamForMemoryModules(ram []byte) []*ScannedModuleInfo {
	var mm []*ScannedModuleInfo
	for i := uint(0); i < uint(len(ram))-12; i++ {
		if ram[i] == 0x87 && ram[i+1] == 0xCD {
			size := wrd(i+2, ram)
			namoff := wrd(i+4, ram)
			check := byt(i+9, ram)
			name := FormatOs9StringFromSlice(i+namoff, ram)
			for j := uint(0); j < 9; j++ {
				check ^= byt(i+j, ram)
			}
			if check != 255 {
				continue
			}

			c1, c2, c3 := byt(i+size-3, ram), byt(i+size-2, ram), byt(i+size-1, ram)
			fullname := fmt.Sprintf("%s.%04x%02x%02x%02x", strings.ToLower(name), size, c1, c2, c3)

			// Logf("ScanRamForMemoryModules: i=%x size=%x namoff=%x=%q check=%x", i, size, namoff, name, check)
			mm = append(mm, &ScannedModuleInfo{
				Name:     name,
				Addy:     i,
				Size:     size,
				FullName: fullname,
			})

		}
	}
	return mm
}

func MemoryModuleOf(addr uint) (name string, offset uint) {
	beginDir, endDir := PPeek2(D_ModDir), PPeek2(D_ModDir+2)

	if beginDir != 0 && endDir != 0 {
		for i := beginDir; i < endDir; i += 4 {
			begin := PPeek2(i)
			if begin < 0x100 {
				continue
			}

			magic := Peek2(begin)
			if magic != 0x87CD {
				continue
			}

			modSize := Peek2(begin + 2)
			// Logf("MM? [%02x] %q %04x (%04x) %04x", i, ModuleId(begin), begin, addr, begin+modSize)

			if begin <= addr && addr < begin+modSize {
				// Logf("MM YES %q+%04x", ModuleId(begin), addr-begin)
				return ModuleId(begin), addr - begin
			}
		}
	} else if 0x0400 <= addr && addr <= 0xFF00 {
		mm := ScanRamForMemoryModules(trackRam[:])
		// for i, m := range mm {
		// Logf("mm [%d] %x %x %q", i, m.Addy, m.Addy+m.Size, m.Name)
		// }
		for i, m := range mm {
			if m.Addy < addr && addr < m.Addy+m.Size {
				Logf(">> [%d] %x %x %q %q", i, m.Addy, m.Addy+m.Size, m.Name, m.FullName)
				return ModuleId(m.Addy), addr - m.Addy
			}
		}
		Logf("MM NO ScanRam ^^")
		return "^^", addr
	} else {
		// Logf("MM NO ~~")
		return "~~", addr
	}

	// Logf("MM NO ==")
	return "==", addr
}
func ModuleId(begin uint) string {
	namePtr := begin + Peek2(begin+4)
	modname := strings.ToLower(Os9String(namePtr))
	sz := Peek2(begin + 2)
	crc1 := Peek1(begin + sz - 3)
	crc2 := Peek1(begin + sz - 2)
	crc3 := Peek1(begin + sz - 1)
	return fmt.Sprintf("%s.%04x%02x%02x%02x", modname, sz, crc1, crc2, crc3)
}

func Os9String(addr uint) string {
	var buf bytes.Buffer
	for {
		var b byte = Peek1(addr)
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

func FormatOs9Chars(vec []byte) string {
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

func FormatOs9StringFromSlice(addr uint, ram []byte) string {
	a := addr
	var buf bytes.Buffer

	for {
		x := byt(a, ram)
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

	return buf.String()
}
func FormatOs9StringFromRam(addr uint) string {
	a := addr
	var buf bytes.Buffer
	//buf.WriteByte('`')
	for {
		x := Peek1(a)
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

type Regs struct {
	cc, a, b, dp byte
	d, x, y, u   uint
}

func FormatReturn(os9num byte, call *Os9ApiCall, rec *EventRec) (string, *Regs) {
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
				fmt.Fprintf(&buf, "RX=%s=%04x=%q, ", call.RX, rx, FormatOs9StringFromRam(rx))
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

func FormatCall(os9num byte, call *Os9ApiCall, rec *EventRec) (string, *Regs) {
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
				fmt.Fprintf(&buf, "X=%s=%04x=%q, ", call.X, x, FormatOs9StringFromRam(x))
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
