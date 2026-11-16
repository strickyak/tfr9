package main

import (
	"bufio"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

var LISTINGS = flag.String("listings", "", "TurboLab listings: comma-separated .list files and/or directories with <name>.<size><crc>")

// Wire protocol IDs for TurboLab (`on tfr911h run turbolab ...`)
const (
	C_TURBOLAB_START = 186
	C_TRACE_CYCLES   = 187
	C_FAULT          = 188
	T_SIGINT         = 183
)

// Trace flag bitmasks (matches firmware/lib/devices/turbolab_config.h)
const (
	TL_TRACE_X    = 1 << 0
	TL_TRACE_R    = 1 << 1
	TL_TRACE_W    = 1 << 2
	TL_TRACE_I    = 1 << 3
	TL_TRACE_T    = 1 << 4
	TL_TRACE_PLUS = 1 << 5
	TL_TRACE_IDLE = 1 << 6
)

// Cycle kinds (low 4 bits of TraceRecord.kind)
const (
	TL_KIND_IDLE        = 0 // '-'
	TL_KIND_FIC         = 1 // 'x'
	TL_KIND_OPCODE_CONT = 2 // '+'
	TL_KIND_READ        = 3 // 'r'
	TL_KIND_WRITE       = 4 // 'w'
	TL_KIND_IRQ         = 5 // 'i' IRQ
	TL_KIND_FIRQ        = 6 // 'i' FIRQ
	TL_KIND_NMI         = 7 // 'i' NMI
	TL_KIND_RTI         = 8 // 'i' RTI
	TL_KIND_SWI2        = 9 // 't' SWI2
)

// CPU status signal flags (high 4 bits of TraceRecord.kind)
const (
	TL_FLAG_BA   = 0x10 // 'a'
	TL_FLAG_BS   = 0x20 // 's'
	TL_FLAG_LIC  = 0x40 // '_'
	TL_FLAG_BUSY = 0x80 // 'y'
)

// Fault / stop reason codes
const (
	TL_FAULT_RED_PAGE       = 1
	TL_FAULT_ZERO_VECTOR    = 2
	TL_FAULT_MAX_CYCLES     = 3
	TL_FAULT_MAX_TIME       = 4
	TL_FAULT_BRA_SELF       = 5
	TL_FAULT_MAX_WATCHPOINT = 6
	TL_FAULT_SIGINT         = 7
	TL_FAULT_TRACE_COUNT    = 8
)

var TurbolabFaultReasonNames = map[byte]string{
	TL_FAULT_RED_PAGE:       "Red Page Access ($FF04..$FFEF)",
	TL_FAULT_ZERO_VECTOR:    "Zero Interrupt Vector Read ($0000)",
	TL_FAULT_MAX_CYCLES:     "Max Cycles Limit Reached",
	TL_FAULT_MAX_TIME:       "Max Time Limit Reached",
	TL_FAULT_BRA_SELF:       "Infinite Loop (BRA $FE)",
	TL_FAULT_MAX_WATCHPOINT: "Watchpoint Limit Reached",
	TL_FAULT_SIGINT:         "Host Interrupt (SIGINT)",
	TL_FAULT_TRACE_COUNT:    "Trace Count Limit Reached",
}

func tlFormatCC(cc byte) string {
	names := "EFHINZVC"
	var buf strings.Builder
	for i := 7; i >= 0; i-- {
		if (cc & (1 << i)) != 0 {
			buf.WriteByte(names[7-i])
		} else {
			buf.WriteByte('.')
		}
	}
	return buf.String()
}

func tlExpandUser(path string) string {
	if strings.HasPrefix(path, "~/") {
		if home, err := os.UserHomeDir(); err == nil {
			return filepath.Join(home, path[2:])
		}
	}
	return path
}

// ── OS-9 Primordial Module Scanner & Listing Relocator ──

type TlScannedModule struct {
	Name     string
	FullName string // "%s.%04x%06x"
	BaseAddr uint16
	Size     uint16
	Crc      uint32
	CrcHex   string
}

func ScanImageForOs9Modules(ram []byte) []*TlScannedModule {
	var modules []*TlScannedModule
	if len(ram) < 16 {
		return nil
	}
	limit := len(ram) - 9
	if limit > 0xFEF8 {
		limit = 0xFEF8
	}
	for i := 0; i < limit; i++ {
		if ram[i] == 0x87 && ram[i+1] == 0xCD {
			size := binary.BigEndian.Uint16(ram[i+2 : i+4])
			if size < 9 || int(i)+int(size) > len(ram) {
				continue
			}
			namoff := binary.BigEndian.Uint16(ram[i+4 : i+6])
			if namoff >= size {
				continue
			}
			var chk byte
			for j := 0; j < 9; j++ {
				chk ^= ram[i+j]
			}
			if chk != 0xFF {
				continue
			}
			var nameBuf []byte
			for p := int(i) + int(namoff); p < int(i)+int(size); p++ {
				b := ram[p]
				ch := b & 0x7F
				if ch < 32 || ch > 126 {
					nameBuf = nil
					break
				}
				nameBuf = append(nameBuf, ch)
				if (b & 0x80) != 0 {
					break
				}
			}
			if len(nameBuf) == 0 {
				continue
			}
			modName := string(nameBuf)
			c1 := ram[int(i)+int(size)-3]
			c2 := ram[int(i)+int(size)-2]
			c3 := ram[int(i)+int(size)-1]
			crcVal := (uint32(c1) << 16) | (uint32(c2) << 8) | uint32(c3)
			crcHex := fmt.Sprintf("%02X%02X%02X", c1, c2, c3)
			fullName := fmt.Sprintf("%s.%04x%06x", strings.ToLower(modName), size, crcVal)
			modules = append(modules, &TlScannedModule{
				Name:     modName,
				FullName: fullName,
				BaseAddr: uint16(i),
				Size:     size,
				Crc:      crcVal,
				CrcHex:   crcHex,
			})
		}
	}
	return modules
}

type ListingModuleInfo struct {
	IsModule bool
	ModSize  uint16
	Crc      uint32
	CrcHex   string
}

var (
	tlModPattern  = regexp.MustCompile(`(?i)^0000\s+(87CD)([0-9A-Fa-f]{4})[0-9A-Fa-f]*\s+.*?\bmod\b`)
	tlEmodPattern = regexp.MustCompile(`(?i)^[0-9A-Fa-f]{4}\s+([0-9A-Fa-f]{6})\s+.*?\bemod\b`)
	tlParseLabel  = regexp.MustCompile(`^([[:xdigit:]]{4})  +[(].*?[)]:[0-9]{5} +([A-Za-z0-9._$@]+[:]?) *$`)
	tlParseLine   = regexp.MustCompile(`^([[:xdigit:]]{4}) [[:xdigit:]]+ +([(].*?[)]:[0-9]{5})(.*)$`)
)

func InspectListingModule(filename string) (*ListingModuleInfo, error) {
	fd, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer fd.Close()

	scanner := bufio.NewScanner(fd)
	var hasMod0000, hasEmod bool
	var modSize uint16
	var crcHex string
	var crcVal uint32

	for scanner.Scan() {
		line := scanner.Text()
		if !hasMod0000 {
			if m := tlModPattern.FindStringSubmatch(line); m != nil {
				hasMod0000 = true
				if sz, err := strconv.ParseUint(m[2], 16, 16); err == nil {
					modSize = uint16(sz)
				}
			}
		}
		if !hasEmod {
			if m := tlEmodPattern.FindStringSubmatch(line); m != nil {
				hasEmod = true
				crcHex = strings.ToUpper(m[1])
				if val, err := strconv.ParseUint(crcHex, 16, 32); err == nil {
					crcVal = uint32(val)
				}
			}
		}
	}
	if hasMod0000 && hasEmod {
		return &ListingModuleInfo{IsModule: true, ModSize: modSize, Crc: crcVal, CrcHex: crcHex}, nil
	}
	return &ListingModuleInfo{IsModule: false}, nil
}

type ListingSource struct {
	Src         map[uint16]string
	Filename    string
	IsOs9Module bool
	ModuleName  string
	FullName    string
	BaseAddr    uint16
	ModSize     uint16
}

func LoadListingFile(filename string, baseAddr uint16) (*ListingSource, error) {
	fd, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer fd.Close()

	d := make(map[uint16]string)
	scanner := bufio.NewScanner(fd)
	var sawAddr, sawLabel string

	for scanner.Scan() {
		text := scanner.Text()
		if m := tlParseLine.FindStringSubmatch(text); m != nil {
			hexaddr := m[1]
			raw := m[3]
			trimmed := strings.TrimSpace(raw)
			if addr, err := strconv.ParseUint(hexaddr, 16, 16); err == nil && trimmed != "" {
				hasLabel := false
				var line string
				if sawLabel != "" && sawAddr == hexaddr {
					hasLabel = true
					line = sawLabel + " " + trimmed
				} else {
					if len(raw) > 9 && raw[9] != ' ' && raw[9] != '\t' {
						hasLabel = true
					}
					line = trimmed
				}
				sawAddr = ""
				sawLabel = ""
				if !hasLabel {
					line = "  " + line
				}
				d[baseAddr+uint16(addr)] = line
			}
		}
		if ml := tlParseLabel.FindStringSubmatch(text); ml != nil {
			sawAddr = ml[1]
			sawLabel = ml[2]
		}
	}
	return &ListingSource{Src: d, Filename: filename, BaseAddr: baseAddr}, nil
}

func LoadAndRelocateListing(filename string, scannedMods []*TlScannedModule) (*ListingSource, *TlScannedModule, error) {
	info, err := InspectListingModule(filename)
	if err != nil {
		return nil, nil, err
	}
	if !info.IsModule {
		src, err := LoadListingFile(filename, 0)
		return src, nil, err
	}
	var matched *TlScannedModule
	for _, m := range scannedMods {
		if info.CrcHex != "" && m.CrcHex == info.CrcHex {
			matched = m
			break
		}
	}
	if matched == nil && info.ModSize > 0 {
		for _, m := range scannedMods {
			if m.Size == info.ModSize {
				matched = m
				break
			}
		}
	}
	if matched == nil {
		return nil, nil, nil
	}
	src, err := LoadListingFile(filename, matched.BaseAddr)
	if err != nil {
		return nil, nil, err
	}
	src.IsOs9Module = true
	src.ModuleName = matched.Name
	src.FullName = matched.FullName
	src.ModSize = matched.Size
	return src, matched, nil
}

type MultiListings []*ListingSource

func (ml MultiListings) Lookup(addr uint16) string {
	for _, l := range ml {
		if s, ok := l.Src[addr]; ok && s != "" {
			return s
		}
	}
	return ""
}

// ── OS-9 SWI2 / RTI System Call Tracer ──

type PendingSwi2 struct {
	Serial uint
	Label  string
	PC     uint16
	Cycle  uint64
	Os9Num byte
	Call   *Os9ApiCall
	Stack  uint16
}

type TlRegs struct {
	CC byte
	A  byte
	B  byte
	DP byte
	X  uint16
	Y  uint16
	U  uint16
	PC uint16
	D  uint16
}

type Os9Tracer struct {
	Out     io.Writer
	Serial  uint
	Pending map[uint]*PendingSwi2
	Ram     []byte

	sawPrefix10    bool
	prefix10Addr   uint16
	prefix10Cycle  uint64
	swi2State      int
	swi2PC         uint16
	swi2Cycle      uint64
	swi2Num        byte
	swi2WriteCount int
	swi2WriteStack uint16
	swi2WriteBytes [12]byte

	rtiState         int
	rtiPC            uint16
	rtiCycle         uint64
	rtiStack         uint16
	rtiReadCount     int
	rtiExpectedReads int
	rtiReadBytes     [12]byte
}

func NewOs9Tracer(out io.Writer, ram []byte) *Os9Tracer {
	tRam := make([]byte, 65536)
	if ram != nil {
		copy(tRam, ram)
	}
	return &Os9Tracer{
		Out:     out,
		Pending: make(map[uint]*PendingSwi2),
		Ram:     tRam,
	}
}

func (t *Os9Tracer) FormatOs9StringFromRam(addr uint16) string {
	if t == nil || t.Ram == nil {
		return ""
	}
	var buf strings.Builder
	for i := 0; i < 20; i++ {
		a := int(addr) + i
		if a >= len(t.Ram) {
			break
		}
		b := t.Ram[a]
		ch := b & 0x7F
		if ch <= 32 || ch > 126 || ch == ',' {
			break
		}
		buf.WriteByte(ch)
		if (b & 0x80) != 0 {
			break
		}
	}
	return buf.String()
}

func (t *Os9Tracer) FormatCall(os9num byte, call *Os9ApiCall, regs *TlRegs) string {
	var buf strings.Builder
	if call == nil {
		fmt.Fprintf(&buf, "$%02X = UNKNOWN ( D=$%04X, X=$%04X, Y=$%04X, U=$%04X )", os9num, regs.D, regs.X, regs.Y, regs.U)
		return buf.String()
	}
	fmt.Fprintf(&buf, "$%02X = %s ( ", os9num, call.Name)
	hasArg := false
	if call.A != "" {
		fmt.Fprintf(&buf, "A=%s=$%02X, ", call.A, regs.A)
		hasArg = true
	}
	if call.B != "" {
		if call.B == "errnum" {
			if errname := Os9ApiErrorNames[regs.B]; errname != "" {
				fmt.Fprintf(&buf, "B=%s=$%02X=%s, ", call.B, regs.B, errname)
			} else {
				fmt.Fprintf(&buf, "B=%s=$%02X, ", call.B, regs.B)
			}
		} else {
			fmt.Fprintf(&buf, "B=%s=$%02X, ", call.B, regs.B)
		}
		hasArg = true
	}
	if call.D != "" {
		fmt.Fprintf(&buf, "D=%s=$%04X, ", call.D, regs.D)
		hasArg = true
	}
	if call.X != "" {
		if strings.HasPrefix(call.X, "$") && t != nil {
			name := t.FormatOs9StringFromRam(regs.X)
			fmt.Fprintf(&buf, "X=%s=$%04X=%q, ", call.X, regs.X, name)
		} else {
			fmt.Fprintf(&buf, "X=%s=$%04X, ", call.X, regs.X)
		}
		hasArg = true
	}
	if call.Y != "" {
		if strings.HasPrefix(call.Y, "$") && t != nil {
			name := t.FormatOs9StringFromRam(regs.Y)
			fmt.Fprintf(&buf, "Y=%s=$%04X=%q, ", call.Y, regs.Y, name)
		} else {
			fmt.Fprintf(&buf, "Y=%s=$%04X, ", call.Y, regs.Y)
		}
		hasArg = true
	}
	if call.U != "" {
		if strings.HasPrefix(call.U, "$") && t != nil {
			name := t.FormatOs9StringFromRam(regs.U)
			fmt.Fprintf(&buf, "U=%s=$%04X=%q, ", call.U, regs.U, name)
		} else {
			fmt.Fprintf(&buf, "U=%s=$%04X, ", call.U, regs.U)
		}
		hasArg = true
	}
	if !hasArg {
		buf.WriteString(")")
		return buf.String()
	}
	s := buf.String()
	if strings.HasSuffix(s, ", ") {
		return s[:len(s)-2] + " )"
	}
	return s + ")"
}

func FormatOs9Return(call *Os9ApiCall, regs *TlRegs) string {
	if (regs.CC & 1) != 0 {
		errname := Os9ApiErrorNames[regs.B]
		errdesc := strings.TrimSpace(Os9ApiErrorDescription[regs.B])
		if errname != "" {
			if errdesc != "" {
				return fmt.Sprintf("ERROR $%02X (%d.) %s: %s", regs.B, regs.B, errname, errdesc)
			}
			return fmt.Sprintf("ERROR $%02X (%d.) %s", regs.B, regs.B, errname)
		}
		return fmt.Sprintf("ERROR $%02X (%d.)", regs.B, regs.B)
	}
	var buf strings.Builder
	buf.WriteString("( ")
	hasRet := false
	if call != nil {
		if call.RA != "" {
			fmt.Fprintf(&buf, "RA=%s=$%02X, ", call.RA, regs.A)
			hasRet = true
		}
		if call.RB != "" {
			fmt.Fprintf(&buf, "RB=%s=$%02X, ", call.RB, regs.B)
			hasRet = true
		}
		if call.RD != "" {
			fmt.Fprintf(&buf, "RD=%s=$%04X, ", call.RD, regs.D)
			hasRet = true
		}
		if call.RX != "" {
			fmt.Fprintf(&buf, "RX=%s=$%04X, ", call.RX, regs.X)
			hasRet = true
		}
		if call.RY != "" {
			fmt.Fprintf(&buf, "RY=%s=$%04X, ", call.RY, regs.Y)
			hasRet = true
		}
		if call.RU != "" {
			fmt.Fprintf(&buf, "RU=%s=$%04X, ", call.RU, regs.U)
			hasRet = true
		}
	}
	if !hasRet {
		if call == nil {
			fmt.Fprintf(&buf, "RD=$%04X, RX=$%04X, RY=$%04X, RU=$%04X, ", regs.D, regs.X, regs.Y, regs.U)
		} else {
			buf.WriteString("ok")
		}
	}
	s := buf.String()
	if strings.HasSuffix(s, ", ") {
		return s[:len(s)-2] + " )"
	}
	return s + " )"
}

func (t *Os9Tracer) OnCycle(rawKind byte, addr uint16, data byte, cycle uint64) {
	kind := rawKind & 0x0F
	flags := rawKind & 0xF0

	if t.Ram != nil && kind != TL_KIND_IDLE {
		t.Ram[addr] = data
	}

	if (kind == TL_KIND_FIC || (flags&TL_FLAG_LIC) != 0) && data == 0x10 && addr < 0xFFF0 {
		t.sawPrefix10 = true
		t.prefix10Addr = addr
		t.prefix10Cycle = cycle
	} else if t.sawPrefix10 {
		t.sawPrefix10 = false
		if data == 0x3F && addr == t.prefix10Addr+1 {
			t.swi2PC = t.prefix10Addr
			t.swi2Cycle = t.prefix10Cycle
			t.swi2State = 1
		}
	} else if t.swi2State == 1 {
		if kind != TL_KIND_WRITE && kind != TL_KIND_IDLE {
			t.swi2Num = data
			t.swi2State = 2
			t.swi2WriteCount = 0
		}
	} else if t.swi2State == 2 {
		if kind == TL_KIND_WRITE {
			t.swi2WriteBytes[11-t.swi2WriteCount] = data
			t.swi2WriteStack = addr
			t.swi2WriteCount++
			if t.swi2WriteCount == 12 {
				t.swi2State = 0
				t.handleSwi2Call()
			}
		} else if kind == TL_KIND_FIC {
			t.swi2State = 0
		}
	}

	if (kind == TL_KIND_FIC || (flags&TL_FLAG_LIC) != 0) && data == 0x3B && addr < 0xFFF0 {
		t.rtiPC = addr
		t.rtiCycle = cycle
		t.rtiState = 1
		t.rtiReadCount = 0
	} else if t.rtiState == 1 {
		if kind != TL_KIND_WRITE && kind != TL_KIND_IDLE {
			if addr != t.rtiPC+1 {
				t.rtiStack = addr
				t.rtiReadBytes[0] = data
				t.rtiReadCount = 1
				if (data & 0x80) != 0 {
					t.rtiExpectedReads = 12
				} else {
					t.rtiExpectedReads = 3
				}
				t.rtiState = 2
			}
		}
	} else if t.rtiState == 2 {
		if kind != TL_KIND_WRITE && kind != TL_KIND_IDLE {
			t.rtiReadBytes[t.rtiReadCount] = data
			t.rtiReadCount++
			if t.rtiReadCount == t.rtiExpectedReads {
				t.rtiState = 0
				t.handleRti()
			}
		} else if kind == TL_KIND_FIC {
			t.rtiState = 0
		}
	}
}

func (t *Os9Tracer) handleSwi2Call() {
	cc, a, b, dp := t.swi2WriteBytes[0], t.swi2WriteBytes[1], t.swi2WriteBytes[2], t.swi2WriteBytes[3]
	x := (uint16(t.swi2WriteBytes[4]) << 8) | uint16(t.swi2WriteBytes[5])
	y := (uint16(t.swi2WriteBytes[6]) << 8) | uint16(t.swi2WriteBytes[7])
	u := (uint16(t.swi2WriteBytes[8]) << 8) | uint16(t.swi2WriteBytes[9])
	pc := (uint16(t.swi2WriteBytes[10]) << 8) | uint16(t.swi2WriteBytes[11])
	regs := &TlRegs{CC: cc, A: a, B: b, DP: dp, X: x, Y: y, U: u, PC: pc, D: (uint16(a) << 8) | uint16(b)}

	t.Serial++
	label := fmt.Sprintf("_%d_", t.Serial)
	call := Os9ApiCallOf[t.swi2Num]
	callDesc := t.FormatCall(t.swi2Num, call, regs)

	if t.swi2Num == 0x06 || t.swi2Num == 0x05 {
		fmt.Fprintf(t.Out, "i SWI2 %s #%d at $%04X: %s (never returns)\n", label, t.swi2Cycle, t.swi2PC, callDesc)
	} else {
		fmt.Fprintf(t.Out, "i SWI2 %s #%d at $%04X: %s\n", label, t.swi2Cycle, t.swi2PC, callDesc)
		key := (uint(t.swi2WriteStack) << 16) | uint(t.swi2PC)
		t.Pending[key] = &PendingSwi2{
			Serial: t.Serial,
			Label:  label,
			PC:     t.swi2PC,
			Cycle:  t.swi2Cycle,
			Os9Num: t.swi2Num,
			Call:   call,
			Stack:  t.swi2WriteStack,
		}
	}
}

func (t *Os9Tracer) handleRti() {
	if t.rtiExpectedReads == 12 {
		cc, a, b, dp := t.rtiReadBytes[0], t.rtiReadBytes[1], t.rtiReadBytes[2], t.rtiReadBytes[3]
		x := (uint16(t.rtiReadBytes[4]) << 8) | uint16(t.rtiReadBytes[5])
		y := (uint16(t.rtiReadBytes[6]) << 8) | uint16(t.rtiReadBytes[7])
		u := (uint16(t.rtiReadBytes[8]) << 8) | uint16(t.rtiReadBytes[9])
		pc := (uint16(t.rtiReadBytes[10]) << 8) | uint16(t.rtiReadBytes[11])
		regs := &TlRegs{CC: cc, A: a, B: b, DP: dp, X: x, Y: y, U: u, PC: pc, D: (uint16(a) << 8) | uint16(b)}

		swi2PC := pc - 3
		key := (uint(t.rtiStack) << 16) | uint(swi2PC)
		pending, ok := t.Pending[key]
		if !ok {
			for k, p := range t.Pending {
				if p.Stack == t.rtiStack {
					pending = p
					ok = true
					delete(t.Pending, k)
					break
				}
			}
		} else {
			delete(t.Pending, key)
		}

		if ok {
			callName := "UNKNOWN"
			if pending.Call != nil {
				callName = pending.Call.Name
			}
			retDesc := FormatOs9Return(pending.Call, regs)
			fmt.Fprintf(t.Out, "i RTI  %s #%d returning to SWI2 $%02X (%s): %s\n",
				pending.Label, t.rtiCycle, pending.Os9Num, callName, retDesc)
		} else {
			fmt.Fprintf(t.Out, "i RTI      #%d returning from interrupt to $%04X\n", t.rtiCycle, pc)
		}
	} else if t.rtiExpectedReads == 3 {
		pc := (uint16(t.rtiReadBytes[1]) << 8) | uint16(t.rtiReadBytes[2])
		fmt.Fprintf(t.Out, "i RTI      #%d returning from FIRQ to $%04X\n", t.rtiCycle, pc)
	}
}

// ── Cycle Trace Formatter & Speed Estimator ──

func tlFormatSuffix(sigStr, comment string) string {
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
	Modules      []*TlScannedModule
	TraceBitmask int
	WatchedAddrs map[uint16][]byte
	Os9Tracer    *Os9Tracer
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
			if kind == TL_KIND_READ || kind == TL_KIND_FIC || kind == TL_KIND_OPCODE_CONT {
				return true
			}
		case 'w':
			if kind == TL_KIND_WRITE {
				return true
			}
		case 'x':
			if kind == TL_KIND_FIC {
				return true
			}
		}
	}
	return false
}

func (tf *TraceFormatter) FindModule(addr uint16) (string, uint16, bool) {
	for _, l := range tf.Listings {
		if l.IsOs9Module && l.FullName != "" && l.ModSize > 0 {
			if addr >= l.BaseAddr && addr < l.BaseAddr+l.ModSize {
				return l.FullName, addr - l.BaseAddr, true
			}
		}
	}
	for _, m := range tf.Modules {
		if addr >= m.BaseAddr && addr < m.BaseAddr+m.Size {
			return m.FullName, addr - m.BaseAddr, true
		}
	}
	return "", 0, false
}

func (tf *TraceFormatter) FormatCycle(rawKind byte, addr uint16, data byte, cycle uint64) {
	if tf.Os9Tracer != nil {
		tf.Os9Tracer.OnCycle(rawKind, addr, data, cycle)
	}

	kind := rawKind & 0x0F
	flags := rawKind & 0xF0

	var sigStr string
	if (flags & TL_FLAG_BA) != 0 {
		sigStr += "a"
	}
	if (flags & TL_FLAG_BS) != 0 {
		sigStr += "s"
	}
	if (flags & TL_FLAG_LIC) != 0 {
		sigStr += "_"
	}
	if (flags & TL_FLAG_BUSY) != 0 {
		sigStr += "y"
	}

	isWatched := tf.IsWatched(addr, kind)

	switch kind {
	case TL_KIND_IDLE:
		if !isWatched && (tf.TraceBitmask&TL_TRACE_IDLE) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "- ---- -- #%d%s\n", cycle, tlFormatSuffix(sigStr, ""))

	case TL_KIND_FIC:
		isRti := (data == 0x3B)
		if !isWatched && (tf.TraceBitmask&TL_TRACE_X) == 0 && !(isRti && (tf.TraceBitmask&TL_TRACE_I) != 0) {
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
		fmt.Fprintf(tf.Out, "x %04X %02X #%d%s\n", addr, data, cycle, tlFormatSuffix(sigStr, comment))

	case TL_KIND_OPCODE_CONT:
		if !isWatched && (tf.TraceBitmask&TL_TRACE_PLUS) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "+ %04X %02X #%d%s\n", addr, data, cycle, tlFormatSuffix(sigStr, ""))

	case TL_KIND_READ:
		isInterruptVector := (addr >= 0xFFF0 && addr <= 0xFFFD) && ((flags & TL_FLAG_BS) != 0)
		isResetVector := (addr == 0xFFFE || addr == 0xFFFF)
		if !isWatched && (tf.TraceBitmask&TL_TRACE_R) == 0 && !((isInterruptVector || isResetVector) && (tf.TraceBitmask&TL_TRACE_I) != 0) {
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
		fmt.Fprintf(tf.Out, "r %04X %02X #%d%s\n", addr, data, cycle, tlFormatSuffix(sigStr, comment))

	case TL_KIND_WRITE:
		if !isWatched && (tf.TraceBitmask&TL_TRACE_W) == 0 {
			return
		}
		fmt.Fprintf(tf.Out, "w %04X %02X #%d%s\n", addr, data, cycle, tlFormatSuffix(sigStr, ""))

	case TL_KIND_IRQ:
		if (tf.TraceBitmask & TL_TRACE_I) != 0 {
			fmt.Fprintf(tf.Out, "i IRQ     #%d%s\n", cycle, tlFormatSuffix(sigStr, ""))
		}
	case TL_KIND_FIRQ:
		if (tf.TraceBitmask & TL_TRACE_I) != 0 {
			fmt.Fprintf(tf.Out, "i FIRQ    #%d%s\n", cycle, tlFormatSuffix(sigStr, ""))
		}
	case TL_KIND_NMI:
		if (tf.TraceBitmask & TL_TRACE_I) != 0 {
			fmt.Fprintf(tf.Out, "i NMI     #%d%s\n", cycle, tlFormatSuffix(sigStr, ""))
		}
	case TL_KIND_RTI:
		if (tf.TraceBitmask & TL_TRACE_I) != 0 {
			fmt.Fprintf(tf.Out, "i RTI     #%d%s\n", cycle, tlFormatSuffix(sigStr, ""))
		}
	case TL_KIND_SWI2:
		if (tf.TraceBitmask & TL_TRACE_T) != 0 {
			fmt.Fprintf(tf.Out, "t SWI2    #%d%s\n", cycle, tlFormatSuffix(sigStr, ""))
		}
	default:
		fmt.Fprintf(tf.Out, "? %04X %02X #%d%s\n", addr, data, cycle, tlFormatSuffix(sigStr, ""))
	}
}

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

func (e *CycleSpeedEstimator) PrintReport() {
	if e == nil || !e.enabled {
		return
	}
	e.once.Do(func() {
		e.mu.Lock()
		if e.firstTime.IsZero() || e.lastCycle <= e.firstCycle {
			e.mu.Unlock()
			return
		}
		elapsed := e.nowFunc().Sub(e.firstTime)
		totalCycles := e.lastCycle - e.firstCycle
		e.mu.Unlock()
		if elapsed <= 0 {
			return
		}
		cps := float64(totalCycles) / elapsed.Seconds()
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

// ── Unified Tether Session State for `on tfr911h run turbolab` ──

type TurbolabSession struct {
	TraceFmt      *TraceFormatter
	Speed         *CycleSpeedEstimator
	FaultSeen     bool
	FaultReason   byte
	InitialLoaded bool
	CoreChunks    int
}

var (
	turbolabActive   atomic.Bool
	turbolabDoneChan = make(chan struct{})
	turbolabDoneOnce sync.Once
	gTurbolab        *TurbolabSession
)

func NotifyTurbolabDone() {
	turbolabDoneOnce.Do(func() {
		close(turbolabDoneChan)
	})
}

func HandleTurbolabStart(pkt []byte) {
	if len(pkt) < 3 {
		return
	}
	traceMask := int(pkt[1])
	numWatches := int(pkt[2])
	watchedMap := make(map[uint16][]byte)
	for i := 0; i < numWatches && 3+i*3+3 <= len(pkt); i++ {
		wType := pkt[3+i*3]
		addr := binary.BigEndian.Uint16(pkt[3+i*3+1 : 3+i*3+3])
		watchedMap[addr] = append(watchedMap[addr], wType)
	}
	gTurbolab = &TurbolabSession{
		TraceFmt: &TraceFormatter{
			Out:          os.Stderr,
			TraceBitmask: traceMask,
			WatchedAddrs: watchedMap,
		},
		Speed: NewCycleSpeedEstimator(traceMask != 0 || numWatches > 0),
	}
	gTurbolab.Speed.OnCycle(0)
	turbolabActive.Store(true)
	CurrentPhase = U_Phase
}

func (s *TurbolabSession) OnInitialRamReady(ram64k []byte) {
	if s == nil || s.InitialLoaded || len(ram64k) < 65536 {
		return
	}
	s.InitialLoaded = true
	s.CoreChunks = 0

	mods := ScanImageForOs9Modules(ram64k[:65536])
	s.TraceFmt.Modules = mods
	for _, m := range mods {
		fmt.Fprintf(os.Stderr, "Found primordial module %q at $%04X-$%04X (size $%04X, CRC %s)\n",
			m.Name, m.BaseAddr, m.BaseAddr+m.Size, m.Size, m.CrcHex)
	}

	loadedMods := make(map[string]bool)
	var listItems []string
	for _, raw := range []string{*LISTINGS, *ABSLISTS, *LINKLISTS} {
		for _, p := range strings.Split(raw, ",") {
			p = strings.TrimSpace(p)
			if p != "" {
				listItems = append(listItems, tlExpandUser(p))
			}
		}
	}

	var listDirs []string
	for _, item := range listItems {
		fi, err := os.Stat(item)
		if err != nil {
			continue
		}
		if fi.IsDir() {
			listDirs = append(listDirs, item)
			continue
		}
		l, matchedMod, err := LoadAndRelocateListing(item, mods)
		if err == nil && l != nil {
			s.TraceFmt.Listings = append(s.TraceFmt.Listings, l)
			if matchedMod != nil {
				loadedMods[matchedMod.FullName] = true
				fmt.Fprintf(os.Stderr, "Loaded listing %q relocated to $%04X for module %q (%d lines)\n",
					item, l.BaseAddr, matchedMod.Name, len(l.Src))
			} else {
				fmt.Fprintf(os.Stderr, "Loaded absolute listing %q (%d lines)\n", item, len(l.Src))
			}
		}
	}

	for _, dir := range listDirs {
		for _, m := range mods {
			if loadedMods[m.FullName] {
				continue
			}
			path := filepath.Join(dir, m.FullName)
			if _, err := os.Stat(path); err != nil {
				if _, err2 := os.Stat(path + ".list"); err2 == nil {
					path = path + ".list"
				} else {
					continue
				}
			}
			if l, err := LoadListingFile(path, m.BaseAddr); err == nil && l != nil {
				l.IsOs9Module = true
				l.ModuleName = m.Name
				l.FullName = m.FullName
				l.ModSize = m.Size
				s.TraceFmt.Listings = append(s.TraceFmt.Listings, l)
				loadedMods[m.FullName] = true
				fmt.Fprintf(os.Stderr, "Loaded listing %q from %s for module %q at $%04X (%d lines)\n",
					filepath.Base(path), dir, m.Name, m.BaseAddr, len(l.Src))
			}
		}
	}

	if (s.TraceFmt.TraceBitmask & TL_TRACE_I) != 0 {
		s.TraceFmt.Os9Tracer = NewOs9Tracer(os.Stderr, ram64k[:65536])
	}
}

func HandleTurbolabTraceCycles(pkt []byte) {
	if gTurbolab == nil || len(pkt) < 2 {
		return
	}
	count := int(pkt[1])
	for i := 0; i < count; i++ {
		off := 2 + i*12
		if off+12 <= len(pkt) {
			cy := binary.LittleEndian.Uint64(pkt[off : off+8])
			addr := binary.BigEndian.Uint16(pkt[off+8 : off+10])
			data := pkt[off+10]
			kind := pkt[off+11]
			gTurbolab.Speed.OnCycle(cy)
			gTurbolab.TraceFmt.FormatCycle(kind, addr, data, cy)
		}
	}
}

func HandleTurbolabFault(pkt []byte) {
	if len(pkt) < 12 {
		return
	}
	if gTurbolab == nil {
		gTurbolab = &TurbolabSession{
			TraceFmt: &TraceFormatter{Out: os.Stderr},
			Speed:    NewCycleSpeedEstimator(false),
		}
	}
	gTurbolab.FaultSeen = true
	gTurbolab.CoreChunks = 0
	reason := pkt[1]
	gTurbolab.FaultReason = reason
	cy := binary.LittleEndian.Uint64(pkt[2:10])
	addr := binary.BigEndian.Uint16(pkt[10:12])
	var data byte
	if len(pkt) >= 13 {
		data = pkt[12]
	}
	gTurbolab.Speed.OnCycle(cy)
	reasonStr, ok := TurbolabFaultReasonNames[reason]
	if !ok {
		reasonStr = fmt.Sprintf("Unknown Fault ($%02X)", reason)
	}
	fmt.Fprintf(os.Stderr, "\n*** PICO FAULT: %s at Cycle #%d (Addr=$%04X, Data=$%02X) ***\n",
		reasonStr, cy, addr, data)

	if len(pkt) >= 31 && pkt[13] != 0 {
		isNative := pkt[14] != 0
		pc := binary.BigEndian.Uint16(pkt[15:17])
		s := binary.BigEndian.Uint16(pkt[17:19])
		u := binary.BigEndian.Uint16(pkt[19:21])
		y := binary.BigEndian.Uint16(pkt[21:23])
		x := binary.BigEndian.Uint16(pkt[23:25])
		dp, a, b, e, f, cc := pkt[25], pkt[26], pkt[27], pkt[28], pkt[29], pkt[30]

		modeStr := "6809"
		if isNative {
			modeStr = "6309 native"
		}
		fmt.Fprintf(os.Stderr, "=== CPU Registers (SWI Capture, %s mode) ===\n", modeStr)
		fmt.Fprintf(os.Stderr, "  PC: $%04X   S: $%04X   U: $%04X   X: $%04X   Y: $%04X\n", pc, s, u, x, y)
		if isNative {
			fmt.Fprintf(os.Stderr, "   D: $%02X%02X (A=$%02X B=$%02X)   W: $%02X%02X (E=$%02X F=$%02X)\n", a, b, a, b, e, f, e, f)
		} else {
			fmt.Fprintf(os.Stderr, "   D: $%02X%02X (A=$%02X B=$%02X)\n", a, b, a, b)
		}
		fmt.Fprintf(os.Stderr, "  DP:   $%02X   CC:   $%02X [%s]\n", dp, cc, tlFormatCC(cc))

		src := gTurbolab.TraceFmt.Listings.Lookup(pc)
		modName, offset, hasMod := gTurbolab.TraceFmt.FindModule(pc)
		var locStr string
		if hasMod {
			locStr = fmt.Sprintf("%s+$%04X", strings.ToLower(modName), offset)
			if src != "" {
				locStr += "  " + src
			}
		} else if src != "" {
			locStr = src
		}
		if locStr != "" {
			fmt.Fprintf(os.Stderr, "  At: $%04X: %s\n", pc, locStr)
		}
		fmt.Fprintf(os.Stderr, "=============================================\n")
	} else if len(pkt) >= 31 {
		fmt.Fprintf(os.Stderr, "(CPU register dump not available)\n")
	}
}

func HandleTurbolabCoreChunk(chunk uint, ram Rammer) {
	if gTurbolab == nil || ram == nil {
		return
	}
	gTurbolab.CoreChunks++
	if chunk != 255 {
		return
	}
	trackRam := ram.GetTrackRam()
	if len(trackRam) < 65536 {
		return
	}
	if !gTurbolab.FaultSeen {
		gTurbolab.OnInitialRamReady(trackRam[:65536])
		return
	}
	fullDump := trackRam[:65536]
	timestamp := time.Now().Format("20060102-150405")
	dumpFile := fmt.Sprintf("/tmp/turbolab-fault-%s.img", timestamp)
	_ = os.WriteFile(dumpFile, fullDump, 0644)
	_ = os.WriteFile("/tmp/fault.img", fullDump, 0644)
	fmt.Fprintf(os.Stderr, "Saved 64KB core dump to %s and /tmp/fault.img\n", dumpFile)
}
