package main

import (
	"fmt"
	"io"
	"strings"
)

// Trace kinds matching firmware
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

func (tf *TraceFormatter) FormatCycle(kind byte, addr uint16, data byte, cycle uint64) {
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

		if comment != "" {
			fmt.Fprintf(tf.Out, "x %04X %02X #%d; %s\n", addr, data, cycle, comment)
		} else {
			fmt.Fprintf(tf.Out, "x %04X %02X #%d; \n", addr, data, cycle)
		}

	case KIND_OPCODE_CONT:
		if (tf.TraceBitmask & TRACE_PLUS) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "+ %04X %02X #%d; \n", addr, data, cycle)

	case KIND_READ:
		if (tf.TraceBitmask & TRACE_R) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "r %04X %02X #%d; \n", addr, data, cycle)

	case KIND_WRITE:
		if (tf.TraceBitmask & TRACE_W) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "w %04X %02X #%d; \n", addr, data, cycle)

	case KIND_IRQ:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i IRQ     #%d; \n", cycle)

	case KIND_FIRQ:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i FIRQ    #%d; \n", cycle)

	case KIND_NMI:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i NMI     #%d; \n", cycle)

	case KIND_RTI:
		if (tf.TraceBitmask & TRACE_I) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "i RTI     #%d; \n", cycle)

	case KIND_SWI2:
		if (tf.TraceBitmask & TRACE_T) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "t SWI2    #%d; \n", cycle)

	default:
		fmt.Fprintf(tf.Out, "? %04X %02X #%d; \n", addr, data, cycle)
	}
}
