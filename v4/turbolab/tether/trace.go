package main

import (
	"fmt"
	"io"
	"os"
	"strings"
	"sync"
	"time"
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
	WatchedAddrs map[uint16][]byte // addr -> types (0=any, 'r', 'w', 'x')
}

func (tf *TraceFormatter) IsWatched(addr uint16, kind byte) bool {
	if len(tf.WatchedAddrs) == 0 {
		return false
	}
	types, ok := tf.WatchedAddrs[addr]
	if !ok {
		return false
	}
	for _, t := range types {
		if t == 0 {
			return true
		}
		switch t {
		case 'r':
			if kind == KIND_READ || kind == KIND_FIC || kind == KIND_OPCODE_CONT {
				return true
			}
		case 'w':
			if kind == KIND_WRITE {
				return true
			}
		case 'x':
			if kind == KIND_FIC {
				return true
			}
		}
	}
	return false
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

	isWatched := tf.IsWatched(addr, kind)

	switch kind {
	case KIND_IDLE:
		if !isWatched && (tf.TraceBitmask & TRACE_IDLE) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "- ---- -- #%d%s\n", cycle, formatSuffix(sigStr, ""))

	case KIND_FIC:
		isRti := (data == 0x3B)
		if !isWatched && (tf.TraceBitmask&TRACE_X) == 0 && !(isRti && (tf.TraceBitmask&TRACE_I) != 0) {
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
		if comment == "" && isRti {
			comment = "rti"
		}

		fmt.Fprintf(tf.Out, "x %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, comment))

	case KIND_OPCODE_CONT:
		if !isWatched && (tf.TraceBitmask & TRACE_PLUS) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "+ %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, ""))

	case KIND_READ:
		isInterruptVector := (addr >= 0xFFF0 && addr <= 0xFFFD) && ((flags & FLAG_BS) != 0)
		isResetVector := (addr == 0xFFFE || addr == 0xFFFF)
		if !isWatched && (tf.TraceBitmask&TRACE_R) == 0 && !((isInterruptVector || isResetVector) && (tf.TraceBitmask&TRACE_I) != 0) {
			return
		}
		src := tf.Listings.Lookup(addr)
		var comment string
		if src != "" {
			comment = src
		} else {
			switch addr {
			case 0xFFF0:
				comment = "reserved vector (high)"
			case 0xFFF1:
				comment = "reserved vector (low)"
			case 0xFFF2:
				comment = "SWI3 vector (high)"
			case 0xFFF3:
				comment = "SWI3 vector (low)"
			case 0xFFF4:
				comment = "SWI2 vector (high)"
			case 0xFFF5:
				comment = "SWI2 vector (low)"
			case 0xFFF6:
				comment = "FIRQ vector (high)"
			case 0xFFF7:
				comment = "FIRQ vector (low)"
			case 0xFFF8:
				comment = "IRQ vector (high)"
			case 0xFFF9:
				comment = "IRQ vector (low)"
			case 0xFFFA:
				comment = "SWI vector (high)"
			case 0xFFFB:
				comment = "SWI vector (low)"
			case 0xFFFC:
				comment = "NMI vector (high)"
			case 0xFFFD:
				comment = "NMI vector (low)"
			case 0xFFFE:
				comment = "reset vector (high)"
			case 0xFFFF:
				comment = "reset vector (low)"
			}
		}
		fmt.Fprintf(tf.Out, "r %04X %02X #%d%s\n", addr, data, cycle, formatSuffix(sigStr, comment))

	case KIND_WRITE:
		if !isWatched && (tf.TraceBitmask & TRACE_W) == 0 {
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

// CycleSpeedEstimator calculates clock cycles per second from the first
// cycle report received until tether shutdown.
type CycleSpeedEstimator struct {
	mu         sync.Mutex
	enabled    bool
	firstTime  time.Time
	firstCycle uint64
	lastCycle  uint64
	once       sync.Once
	Stderr     io.Writer
	Stdout     io.Writer
	nowFunc    func() time.Time
}

func NewCycleSpeedEstimator(enabled bool) *CycleSpeedEstimator {
	return &CycleSpeedEstimator{
		enabled: enabled,
		Stderr:  os.Stderr,
		Stdout:  os.Stdout,
		nowFunc: time.Now,
	}
}

func (e *CycleSpeedEstimator) OnCycle(cy uint64) {
	if e == nil || !e.enabled {
		return
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.firstTime.IsZero() {
		e.firstTime = e.nowFunc()
		e.firstCycle = cy
	}
	e.lastCycle = cy
}

func (e *CycleSpeedEstimator) Calculate() (cps float64, totalCycles uint64, elapsed time.Duration, ok bool) {
	if e == nil || !e.enabled {
		return 0, 0, 0, false
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.firstTime.IsZero() || e.lastCycle <= e.firstCycle {
		return 0, 0, 0, false
	}
	now := e.nowFunc()
	elapsed = now.Sub(e.firstTime)
	if elapsed <= 0 {
		return 0, 0, 0, false
	}
	totalCycles = e.lastCycle - e.firstCycle
	cps = float64(totalCycles) / elapsed.Seconds()
	return cps, totalCycles, elapsed, true
}

func (e *CycleSpeedEstimator) PrintReport() {
	if e == nil || !e.enabled {
		return
	}
	e.once.Do(func() {
		cps, totalCycles, elapsed, ok := e.Calculate()
		if !ok {
			return
		}
		msg := fmt.Sprintf("Estimated cycles per second: %.0f (%.3f MHz) [%d cycles in %.2fs]",
			cps, cps/1e6, totalCycles, elapsed.Seconds())
		if e.Stderr != nil {
			fmt.Fprintln(e.Stderr, msg)
		}
		if e.Stdout != nil {
			fmt.Fprintf(e.Stdout, "\n[%s]\n", msg)
		}
	})
}

