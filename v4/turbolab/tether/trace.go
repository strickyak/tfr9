package main

import (
	"fmt"
	"io"
	"strings"
)

// Trace kinds matching firmware (low 4 bits)
const (
	KIND_IDLE        = 0 // '-'
	KIND_FIC         = 1 // 'x'
	KIND_OPCODE_CONT = 2 // '+'
	KIND_READ        = 3 // 'r'
	KIND_WRITE       = 4 // 'w'
	KIND_IRQ         = 5 // 'i' IRQ
	KIND_FIRQ        = 6 // 'i' FIRQ
	KIND_NMI         = 7 // 'i' NMI
	KIND_RTI         = 8 // 'i' RTI
	KIND_SWI2        = 9 // 't' SWI2
)

// CPU signal flags in high 4 bits: a=BA s=BS _=LIC y=BUSY
const (
	FLAG_BA   = 0x10 // 'a'
	FLAG_BS   = 0x20 // 's'
	FLAG_LIC  = 0x40 // '_'
	FLAG_BUSY = 0x80 // 'y'
)

func formatSuffix(sigStr, comment string) string {
	if comment != "" {
		if sigStr != "" {
			return ";" + sigStr + " " + comment
		}
		return "; " + comment
	}
	if sigStr != "" {
		return ";" + sigStr
	}
	return "; "
}

type TraceFormatter struct {
	Out          io.Writer
	Listings     MultiListings
	Modules      []*ScannedModuleInfo
	TraceBitmask int
}

func (tf *TraceFormatter) FindModule(addr uint16) (string, uint16, bool) {
	// First check listings
	for _, l := range tf.Listings {
		if l.IsOs9Module && l.FullName != "" && l.ModSize > 0 {
			if addr >= l.BaseAddr && addr < l.BaseAddr+l.ModSize {
				return l.FullName, addr - l.BaseAddr, true
			}
		}
	}
	// Next check scanned modules in RAM
	for _, m := range tf.Modules {
		if addr >= m.BaseAddr && addr < m.BaseAddr+m.Size {
			return m.FullName, addr - m.BaseAddr, true
		}
	}
	return "", 0, false
}

func (tf *TraceFormatter) FormatCycle(rawKind byte, addr uint16, data byte, cycle uint64) {
	kind := rawKind & 0x0F
	flags := rawKind & 0xF0

	var sigStr string
	if (flags & FLAG_BA) != 0 {
		sigStr += "a"
	}
	if (flags & FLAG_BS) != 0 {
		sigStr += "s"
	}
	if (flags & FLAG_LIC) != 0 {
		sigStr += "_"
	}
	if (flags & FLAG_BUSY) != 0 {
		sigStr += "y"
	}

	switch kind {
	case KIND_IDLE:
		// Not currently enabled via flags
		return

	case KIND_FIC:
		if (tf.TraceBitmask & TRACE_X) == 0 {
			return
		}
		src := tf.Listings.Lookup(addr)
		modName, offset, hasMod := tf.FindModule(addr)

		var comment string
		if hasMod {
			prefix := fmt.Sprintf("%q+%04x", strings.ToLower(modName), offset)
			if src != "" {
				comment = prefix + " " + src
			} else {
				comment = prefix
			}
		} else {
			comment = src
		}

		fmt.Fprintf(tf.Out, "x %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, comment))

	case KIND_OPCODE_CONT:
		if (tf.TraceBitmask & TRACE_PLUS) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "+ %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, ""))

	case KIND_READ:
		if (tf.TraceBitmask & TRACE_R) == 0 {
			return
		}
		src := tf.Listings.Lookup(addr)
		var comment string
		if src != "" {
			comment = src
		} else if addr == 0xFFFE {
			comment = "reset vector (high)"
		} else if addr == 0xFFFF {
			comment = "reset vector (low)"
		}
		fmt.Fprintf(tf.Out, "r %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, comment))

	case KIND_WRITE:
		if (tf.TraceBitmask & TRACE_W) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "w %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, ""))

	case KIND_IRQ:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i IRQ     #%d%s\n", cycle, formatSuffix(sigStr, ""))

	case KIND_FIRQ:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i FIRQ    #%d%s\n", cycle, formatSuffix(sigStr, ""))

	case KIND_NMI:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i NMI     #%d%s\n", cycle, formatSuffix(sigStr, ""))

	case KIND_RTI:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i RTI     #%d%s\n", cycle, formatSuffix(sigStr, ""))

	case KIND_SWI2:
		if (tf.TraceBitmask & TRACE_T) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "t SWI2    #%d%s\n", cycle, formatSuffix(sigStr, ""))

	default:
		fmt.Fprintf(tf.Out, "? %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, ""))
	}
}
