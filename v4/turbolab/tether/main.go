package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/strickyak/tfr9/v4/turbolab/tether/cobs"
)

var cpuStarted atomic.Bool

// Command bytes
const (
	C_NOP          = 0
	C_FAULT        = 188
	C_CORE_DUMP    = 189
	C_PUTCHAR      = 193
	C_RESTARTED    = 194
	C_TRACE_CYCLES = 200

	T_REFLASH_NOW_PLEASE = 175
	T_RESTART_NOW_PLEASE = 177
	T_CONSOLE_LINE       = 179
	T_PICO_RPC           = 181
)

// Trace flags
const (
	TRACE_X    = 1 << 0
	TRACE_R    = 1 << 1
	TRACE_W    = 1 << 2
	TRACE_I    = 1 << 3
	TRACE_T    = 1 << 4
	TRACE_PLUS = 1 << 5
	TRACE_IDLE = 1 << 6
)

// Fault reasons
const (
	FAULT_RED_PAGE    = 1
	FAULT_ZERO_VECTOR = 2
	FAULT_MAX_CYCLES  = 3
	FAULT_MAX_TIME    = 4
)

var FaultReasonNames = map[byte]string{
	FAULT_RED_PAGE:    "Red Page Access ($FF04..$FFEF)",
	FAULT_ZERO_VECTOR: "Zero Interrupt Vector Read ($0000)",
	FAULT_MAX_CYCLES:  "Max Cycles Limit Reached",
	FAULT_MAX_TIME:    "Max Time Limit Reached",
}

func formatCC(cc byte) string {
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


// Flags
var (
	flagWire     = flag.String("wire", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
	flagBaud     = flag.Uint("baud", 115200, "serial device baud rate")
	flagTrace    = flag.String("trace", "", "trace flags: comma-separated x,+,r,w,i,t,- (or 1 for all except idle; add idle with 1,idle or 1,-)")
	flagTrigger  = flag.String("trigger", "", "trigger trace on cycle (c:50000) or seconds (s:5)")
	flagMax      = flag.String("max", "", "max cycles (c:1m) or max time (t:30s)")
	flagDebug    = flag.String("debug", "", "debug options (e.g. --debug=u to log packets in and out on stderr)")
	flagListings = flag.String("listings", "", "directory with module listings named <name>.<size><crc>")
	flagReflash  = flag.Bool("reflash", false, "reboot Pico into BOOTSEL mode for reflashing and exit")
	flagExit     = flag.Bool("exit", false, "immediately exit(0) without doing anything")
	flagTuning   = flag.String("tuning", "", "firmware timing tuning: MHZ,K1,K2,T1,T2,... (or key=val pairs, e.g. --tuning=250,K1=9,K2=19,T1=16)")
	flagN        = flag.Bool("n", false, "no stdin: do not configure stty or run ReadLine; wait for completion")
)

func expandUser(path string) string {
	if strings.HasPrefix(path, "~/") {
		home, err := os.UserHomeDir()
		if err == nil {
			return filepath.Join(home, path[2:])
		}
	}
	return path
}

func packetName(cmd byte) string {
	switch cmd {
	case C_NOP:
		return "C_NOP"
	case C_FAULT:
		return "C_FAULT"
	case C_CORE_DUMP:
		return "C_CORE_DUMP"
	case C_PUTCHAR:
		return "C_PUTCHAR"
	case C_RESTARTED:
		return "C_RESTARTED"
	case C_TRACE_CYCLES:
		return "C_TRACE_CYCLES"
	case T_REFLASH_NOW_PLEASE:
		return "T_REFLASH_NOW_PLEASE"
	case T_RESTART_NOW_PLEASE:
		return "T_RESTART_NOW_PLEASE"
	case T_CONSOLE_LINE:
		return "T_CONSOLE_LINE"
	case T_PICO_RPC:
		return "T_PICO_RPC"
	default:
		return fmt.Sprintf("$%02X", cmd)
	}
}

func logPacket(direction string, pkt []byte) {
	if len(pkt) == 0 {
		return
	}
	cmd := pkt[0]
	cmdName := packetName(cmd)
	if len(pkt) <= 64 {
		fmt.Fprintf(os.Stderr, "PACKET %s: cmd=%d(%s) len=%d hex=%x\n", direction, cmd, cmdName, len(pkt), pkt)
	} else {
		fmt.Fprintf(os.Stderr, "PACKET %s: cmd=%d(%s) len=%d hex=%x...\n", direction, cmd, cmdName, len(pkt), pkt[:32])
	}
}

func parseCount(s string) (uint64, error) {
	s = strings.TrimSpace(strings.ToLower(s))
	multiplier := uint64(1)
	if strings.HasSuffix(s, "m") {
		multiplier = 1000000
		s = s[:len(s)-1]
	} else if strings.HasSuffix(s, "k") {
		multiplier = 1000
		s = s[:len(s)-1]
	}
	val, err := strconv.ParseUint(s, 10, 64)
	if err != nil {
		return 0, err
	}
	return val * multiplier, nil
}

func parseDurationUs(s string) (uint64, error) {
	s = strings.TrimSpace(strings.ToLower(s))
	if strings.HasSuffix(s, "us") {
		v, err := strconv.ParseUint(s[:len(s)-2], 10, 64)
		return v, err
	} else if strings.HasSuffix(s, "ms") {
		v, err := strconv.ParseUint(s[:len(s)-2], 10, 64)
		return v * 1000, err
	} else if strings.HasSuffix(s, "s") {
		v, err := strconv.ParseFloat(s[:len(s)-1], 64)
		return uint64(v * 1000000.0), err
	} else if strings.HasSuffix(s, "m") {
		v, err := strconv.ParseFloat(s[:len(s)-1], 64)
		return uint64(v * 60.0 * 1000000.0), err
	}
	v, err := strconv.ParseFloat(s, 64)
	return uint64(v * 1000000.0), err
}

func parseTraceFlags(str string) (int, error) {
	var bitmask int
	if str == "" {
		return 0, nil
	}
	parts := strings.Split(str, ",")
	for _, p := range parts {
		switch strings.TrimSpace(strings.ToLower(p)) {
		case "1", "all":
			// Implies all trace flags except idle (-).
			// To add Idle, must explicitly name it: --trace=1,idle or --trace=1,-
			bitmask |= TRACE_X | TRACE_PLUS | TRACE_R | TRACE_W | TRACE_I | TRACE_T
		case "x":
			bitmask |= TRACE_X
		case "+":
			bitmask |= TRACE_PLUS
		case "r":
			bitmask |= TRACE_R | TRACE_X | TRACE_I | TRACE_PLUS
		case "w":
			bitmask |= TRACE_W
		case "i":
			bitmask |= TRACE_I
		case "t":
			bitmask |= TRACE_T
		case "-", "idle":
			bitmask |= TRACE_IDLE
		default:
			return 0, fmt.Errorf("unknown trace flag: %q (supported: 1, x, +, r, w, i, t, -, idle)", p)
		}
	}
	return bitmask, nil
}

func main() {
	for _, arg := range os.Args[1:] {
		if arg == "--exit" || arg == "-exit" || arg == "--exit=true" || arg == "-exit=true" || arg == "--exit=1" || arg == "-exit=1" {
			os.Exit(0)
		}
	}

	flag.Parse()

	if *flagExit {
		os.Exit(0)
	}

	// Parse trace flags
	traceBitmask, err := parseTraceFlags(*flagTrace)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	// Parse timing tuning flag
	tuningParams, err := ParseTuningFlag(*flagTuning)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Invalid tuning flag: %v\n", err)
		os.Exit(1)
	}
	if *flagTuning != "" {
		fmt.Fprintf(os.Stderr, "Custom timing configured: %s\n", tuningParams.String())
	}

	speedEstimator := NewCycleSpeedEstimator(traceBitmask != 0)

	// Parse trigger
	var triggerCycle uint64
	var triggerTimeUs uint64
	if *flagTrigger != "" {
		parts := strings.SplitN(*flagTrigger, ":", 2)
		if len(parts) != 2 {
			fmt.Fprintf(os.Stderr, "Invalid trigger format %q (expected c:<cycles> or s:<seconds>)\n", *flagTrigger)
			os.Exit(1)
		}
		switch parts[0] {
		case "c":
			c, err := parseCount(parts[1])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Invalid trigger cycle %q: %v\n", parts[1], err)
				os.Exit(1)
			}
			triggerCycle = c
		case "s":
			us, err := parseDurationUs(parts[1])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Invalid trigger time %q: %v\n", parts[1], err)
				os.Exit(1)
			}
			triggerTimeUs = us
		default:
			fmt.Fprintf(os.Stderr, "Unknown trigger type %q (expected 'c' or 's')\n", parts[0])
			os.Exit(1)
		}
	}

	// Parse max limits
	var maxCycles uint64
	var maxTimeUs uint64
	if *flagMax != "" {
		parts := strings.SplitN(*flagMax, ":", 2)
		if len(parts) != 2 {
			fmt.Fprintf(os.Stderr, "Invalid max format %q (expected c:<cycles> or t:<duration>)\n", *flagMax)
			os.Exit(1)
		}
		switch parts[0] {
		case "c":
			c, err := parseCount(parts[1])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Invalid max cycles %q: %v\n", parts[1], err)
				os.Exit(1)
			}
			maxCycles = c
		case "t":
			us, err := parseDurationUs(parts[1])
			if err != nil {
				fmt.Fprintf(os.Stderr, "Invalid max time %q: %v\n", parts[1], err)
				os.Exit(1)
			}
			maxTimeUs = us
		default:
			fmt.Fprintf(os.Stderr, "Unknown max type %q (expected 'c' or 't')\n", parts[0])
			os.Exit(1)
		}
	}

	// Separate positional arguments into image/modules and listings
	var binArgs []string
	var listArgs []string
	for _, arg := range flag.Args() {
		if strings.HasSuffix(arg, ".list") {
			listArgs = append(listArgs, arg)
		} else {
			binArgs = append(binArgs, arg)
		}
	}

	if len(binArgs) == 0 && !*flagReflash {
		fmt.Fprintf(os.Stderr, "Usage: tether [flags] image_file.img | decb_file.decb | module_files... [listings.list...]\n")
		os.Exit(1)
	}

	var ramImage []byte
	var scannedMods []*ScannedModuleInfo
	var listings MultiListings

	if len(binArgs) > 0 {
		var err error
		ramImage, err = PrepareMemoryImage(binArgs)
		if err != nil {
			Fatalf("Error preparing memory image: %v", err)
		}
		fmt.Fprintf(os.Stderr, "Prepared 65536-byte memory image.\n")

		scannedMods = ScanImageForOs9Modules(ramImage)
		for _, m := range scannedMods {
			fmt.Fprintf(os.Stderr, "Found primordial module %q at $%04X-$%04X (size $%04X, CRC %s)\n",
				m.Name, m.BaseAddr, m.BaseAddr+m.Size, m.Size, m.CrcHex)
		}

		loadedMods := make(map[string]bool)

		// 1. Process explicit listing files from command line
		for _, lf := range listArgs {
			l, matchedMod, err := LoadAndRelocateListing(lf, scannedMods)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Warning: cannot load listing %q: %v\n", lf, err)
			} else if l != nil {
				listings = append(listings, l)
				if matchedMod != nil {
					loadedMods[matchedMod.FullName] = true
					fmt.Fprintf(os.Stderr, "Loaded listing %q relocated to $%04X for module %q (%d lines)\n",
						lf, l.BaseAddr, matchedMod.Name, len(l.Src))
				} else {
					fmt.Fprintf(os.Stderr, "Loaded absolute listing %q (%d lines)\n", lf, len(l.Src))
				}
			} else {
				fmt.Fprintf(os.Stderr, "Skipping OS-9 module listing %q (not found in RAM image)\n", lf)
			}
		}

		// 2. If --listings is set, look up any primordial modules not found on command line
		listingsDir := expandUser(*flagListings)
		if listingsDir != "" {
			for _, m := range scannedMods {
				if loadedMods[m.FullName] {
					continue
				}
				path := filepath.Join(listingsDir, m.FullName)
				if _, err := os.Stat(path); err != nil {
					pathWithList := path + ".list"
					if _, err2 := os.Stat(pathWithList); err2 == nil {
						path = pathWithList
					} else {
						continue
					}
				}

				l, err := LoadListingFile(path, m.BaseAddr)
				if err != nil {
					fmt.Fprintf(os.Stderr, "Warning: cannot load directory listing %q: %v\n", path, err)
				} else {
					l.IsOs9Module = true
					l.ModuleName = m.Name
					l.FullName = m.FullName
					l.ModSize = m.Size
					listings = append(listings, l)
					loadedMods[m.FullName] = true
					fmt.Fprintf(os.Stderr, "Loaded listing %q from %s for module %q at $%04X (%d lines)\n",
						filepath.Base(path), *flagListings, m.Name, m.BaseAddr, len(l.Src))
				}
			}
		}
	}

	// Save terminal state and setup cleanup
	if !*flagN {
		SaveSttyState()
		defer RestoreSttyState()
	}

	sigChan := make(chan os.Signal, 2)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-sigChan
		speedEstimator.PrintReport()
		if !*flagN {
			RestoreSttyState()
		}
		if sig == syscall.SIGINT {
			fmt.Printf("\n[SIGINT]\n")
			fmt.Fprintf(os.Stderr, "Interrupted by SIGINT (^C); tether exiting.\n")
			os.Exit(130)
		} else if sig == syscall.SIGTERM {
			fmt.Printf("\n[SIGTERM]\n")
			fmt.Fprintf(os.Stderr, "Terminated by SIGTERM; tether exiting.\n")
			os.Exit(143)
		} else {
			fmt.Printf("\n[%v]\n", sig)
			fmt.Fprintf(os.Stderr, "Terminated by signal %v; tether exiting.\n", sig)
			os.Exit(1)
		}
	}()

	// Connect to serial port
	serialOptions := OpenSerialOptions{
		PortName:        *flagWire,
		BaudRate:        *flagBaud,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

	fmt.Fprintf(os.Stderr, "Connecting to %s at %d baud...\n", *flagWire, *flagBaud)
	serialPort, err := OpenSerial(serialOptions)
	if err != nil {
		fmt.Printf("\n[USB Failed: '%s' %v]\n", *flagWire, err)
		fmt.Fprintf(os.Stderr, "USB Failed: %s: %v\n", *flagWire, err)
		if os.IsNotExist(err) {
			matches, _ := filepath.Glob("/dev/ttyACM*")
			matchesUSB, _ := filepath.Glob("/dev/ttyUSB*")
			allMatches := append(matches, matchesUSB...)
			if len(allMatches) > 0 {
				fmt.Fprintf(os.Stderr, "Hint: Device '%s' not found. Available serial devices: %s\n", *flagWire, strings.Join(allMatches, ", "))
			} else {
				fmt.Fprintf(os.Stderr, "Hint: Device '%s' not found. No /dev/ttyACM* or /dev/ttyUSB* detected.\n      Check USB cable connection or if Pico is still booting / in BOOTSEL mode.\n", *flagWire)
			}
		} else if os.IsPermission(err) {
			fmt.Fprintf(os.Stderr, "Hint: Permission denied for '%s'. Check dialout group membership or udev permissions.\n", *flagWire)
		}
		RestoreSttyState()
		os.Exit(1)
	}
	defer serialPort.Close()

	fmt.Printf("\n[USB Connected: '%s'] ", *flagWire)
	fmt.Fprintf(os.Stderr, "USB Connected: '%s'\n", *flagWire)

	// Disconnect handler
	var isReflashing bool
	var usbDropOnce sync.Once
	onUsbDrop := func(err error) {
		if isReflashing {
			return
		}
		usbDropOnce.Do(func() {
			fmt.Printf("\n[USB Disconnected: '%s']\n", *flagWire)
			fmt.Fprintf(os.Stderr, "USB Disconnected: %s: %v\n", *flagWire, err)
			speedEstimator.PrintReport()
			RestoreSttyState()
			os.Exit(1)
		})
	}

	// Send leading 0 to flush partial frames
	serialPort.Write([]byte{0x00})

	channelToPico := make(chan []byte, 128)
	cobsFromPico := make(chan []byte, 256)

	debugUsb := strings.Contains(*flagDebug, "u")

	// Writer goroutine
	go func() {
		for pkt := range channelToPico {
			if len(pkt) > 0 && pkt[0] == T_RESTART_NOW_PLEASE {
				os.Stdout.WriteString("#")
				fmt.Fprintf(os.Stderr, "Sent RESTART_NOW_PLEASE to Pico\n")
			}
			if debugUsb {
				logPacket("OUT", pkt)
			}
			encoded := cobs.Encode(pkt)
			encoded = append(encoded, 0x00)
			if _, err := serialPort.Write(encoded); err != nil {
				onUsbDrop(err)
				return
			}
		}
	}()

	// Reader goroutine with COBS decoder
	go func() {
		buf := make([]byte, 1024)
		var currentPacket []byte
		for {
			n, err := serialPort.Read(buf)
			if err != nil {
				onUsbDrop(err)
				return
			}
			for i := 0; i < n; i++ {
				b := buf[i]
				if b == 0 {
					if len(currentPacket) > 0 {
						decoded, err := cobs.Decode(currentPacket)
						if err == nil && len(decoded) > 0 {
							cmd := decoded[0]
							if !cpuStarted.Load() && (cmd == C_TRACE_CYCLES || cmd == C_PUTCHAR || cmd == C_FAULT || cmd == C_CORE_DUMP) {
								currentPacket = nil
								continue
							}
							if debugUsb {
								logPacket("IN", decoded)
							}
							cobsFromPico <- decoded
						}
						currentPacket = nil
					}
				} else {
					currentPacket = append(currentPacket, b)
				}
			}
		}
	}()

	// PicoRPC call helper
	var rpcMu sync.Mutex
	var rpcSerial int
	rpcPending := make(map[int]chan RpcResponse)

	picoRpcCall := func(method string, flags int, offset int64, length int, whence int64, data []byte) (RpcResponse, error) {
		rpcMu.Lock()
		rpcSerial++
		serial := rpcSerial
		ch := make(chan RpcResponse, 1)
		rpcPending[serial] = ch
		rpcMu.Unlock()

		req := RpcRequest{
			Method: method,
			Serial: serial,
			Flags:  flags,
			Offset: int(offset),
			Length: length,
			Whence: int(whence),
			Data:   data,
		}
		encoded := EncodeRpcRequest(req)
		packet := append([]byte{T_PICO_RPC}, encoded...)
		channelToPico <- packet

		select {
		case resp := <-ch:
			return resp, nil
		case <-time.After(5 * time.Second):
			rpcMu.Lock()
			delete(rpcPending, serial)
			rpcMu.Unlock()
			return RpcResponse{Status: -1}, fmt.Errorf("RPC timeout for %q (serial %d)", method, serial)
		}
	}

	// ── Background Packet Dispatcher ──
	restartedChan := make(chan struct{}, 1)
	traceFmt := &TraceFormatter{
		Out:          os.Stderr,
		Listings:     listings,
		Modules:      scannedMods,
		TraceBitmask: traceBitmask,
	}

	coreDumpChunks := make(map[byte][]byte)
	var faultReason byte
	var faultCycle uint64
	var faultAddr uint16
	var faultData byte

	go func() {
		for pkt := range cobsFromPico {
			if len(pkt) == 0 {
				continue
			}

			cmd := pkt[0]
			switch cmd {
			case C_RESTARTED:
				os.Stdout.WriteString("%")
				fmt.Fprintf(os.Stderr, "Received C_RESTARTED from Pico\n")
				select {
				case restartedChan <- struct{}{}:
				default:
				}

			case T_PICO_RPC:
				resp := DecodeRpcResponse(pkt[1:])
				rpcMu.Lock()
				ch, ok := rpcPending[resp.Serial]
				if ok {
					delete(rpcPending, resp.Serial)
				}
				rpcMu.Unlock()
				if ok {
					ch <- resp
				}

			case C_PUTCHAR:
				for _, b := range pkt[1:] {
					os.Stdout.Write([]byte{b})
				}

			case C_TRACE_CYCLES:
				if len(pkt) >= 2 {
					count := int(pkt[1])
					for i := 0; i < count; i++ {
						off := 2 + i*12
						if off+12 <= len(pkt) {
							cy := binary.LittleEndian.Uint64(pkt[off : off+8])
							addr := binary.BigEndian.Uint16(pkt[off+8 : off+10])
							data := pkt[off+10]
							kind := pkt[off+11]
							speedEstimator.OnCycle(cy)
							traceFmt.FormatCycle(kind, addr, data, cy)
						}
					}
				}

			case C_FAULT:
				if len(pkt) >= 12 {
					faultReason = pkt[1]
					faultCycle = binary.LittleEndian.Uint64(pkt[2:10])
					faultAddr = binary.BigEndian.Uint16(pkt[10:12])
					if len(pkt) >= 13 {
						faultData = pkt[12]
					}
					speedEstimator.OnCycle(faultCycle)
					reasonStr, ok := FaultReasonNames[faultReason]
					if !ok {
						reasonStr = fmt.Sprintf("Unknown Fault ($%02X)", faultReason)
					}
					fmt.Fprintf(os.Stderr, "\n*** PICO FAULT: %s at Cycle #%d (Addr=$%04X, Data=$%02X) ***\n",
						reasonStr, faultCycle, faultAddr, faultData)

					if len(pkt) >= 31 && pkt[13] != 0 {
						isNative := pkt[14] != 0
						pc := binary.BigEndian.Uint16(pkt[15:17])
						s := binary.BigEndian.Uint16(pkt[17:19])
						u := binary.BigEndian.Uint16(pkt[19:21])
						y := binary.BigEndian.Uint16(pkt[21:23])
						x := binary.BigEndian.Uint16(pkt[23:25])
						dp := pkt[25]
						a := pkt[26]
						b := pkt[27]
						e := pkt[28]
						f := pkt[29]
						cc := pkt[30]

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
						fmt.Fprintf(os.Stderr, "  DP:   $%02X   CC:   $%02X [%s]\n", dp, cc, formatCC(cc))

						src := traceFmt.Listings.Lookup(pc)
						modName, offset, hasMod := traceFmt.FindModule(pc)
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

			case C_CORE_DUMP:
				if len(pkt) >= 2 {
					chunkIdx := pkt[1]
					chunkData := make([]byte, len(pkt)-2)
					copy(chunkData, pkt[2:])
					coreDumpChunks[chunkIdx] = chunkData

					if len(coreDumpChunks) == 64 {
						// All 64KB received
						fullDump := make([]byte, 65536)
						for c := 0; c < 64; c++ {
							if d, ok := coreDumpChunks[byte(c)]; ok {
								copy(fullDump[c*1024:(c+1)*1024], d)
							}
						}
						timestamp := time.Now().Format("20060102-150405")
						dumpFile := fmt.Sprintf("/tmp/turbolab-fault-%s.img", timestamp)
						_ = os.WriteFile(dumpFile, fullDump, 0644)
						_ = os.WriteFile("/tmp/fault.img", fullDump, 0644)
						fmt.Fprintf(os.Stderr, "Saved 64KB core dump to %s and /tmp/fault.img\n", dumpFile)

						// Drain any trailing packets queued in cobsFromPico
						for {
							select {
							case p := <-cobsFromPico:
								if len(p) > 0 && p[0] == C_TRACE_CYCLES {
									if len(p) >= 2 {
										count := int(p[1])
										for i := 0; i < count; i++ {
											off := 2 + i*12
											if off+12 <= len(p) {
												cy := binary.LittleEndian.Uint64(p[off : off+8])
												addr := binary.BigEndian.Uint16(p[off+8 : off+10])
												data := p[off+10]
												kind := p[off+11]
												speedEstimator.OnCycle(cy)
												traceFmt.FormatCycle(kind, addr, data, cy)
											}
										}
									}
								} else if len(p) > 0 && p[0] == C_PUTCHAR {
									for _, b := range p[1:] {
										os.Stdout.Write([]byte{b})
									}
								}
							default:
								goto allDrained
							}
						}
					allDrained:
						speedEstimator.PrintReport()
						RestoreSttyState()
						if faultReason == FAULT_MAX_CYCLES || faultReason == FAULT_MAX_TIME {
							os.Exit(0)
						} else {
							os.Exit(1)
						}
					}
				}
			}
		}
	}()

	// ── Phase 1: Wait for Pico "Restarted" beacon ──
	fmt.Fprintf(os.Stderr, "Waiting for Pico 'Restarted' beacon...\n")
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()

	restarted := false
	for !restarted {
		select {
		case <-restartedChan:
			fmt.Fprintf(os.Stderr, "Pico detected (C_RESTARTED received).\n")
			restarted = true
		case <-ticker.C:
			fmt.Fprintf(os.Stderr, "No beacon heard after 2s; sending RESTART_NOW_PLEASE to Pico...\n")
			channelToPico <- []byte{T_RESTART_NOW_PLEASE}
		}
	}
	os.Stdout.WriteString("\n")

	if *flagReflash {
		isReflashing = true
		channelToPico <- []byte{T_REFLASH_NOW_PLEASE}
		fmt.Printf("\n[Sent T_REFLASH_NOW_PLEASE to Pico]\n")
		fmt.Fprintf(os.Stderr, "Sent T_REFLASH_NOW_PLEASE to Pico\n")
		time.Sleep(1 * time.Second)
		go func() { _ = serialPort.Close() }()
		RestoreSttyState()
		os.Exit(0)
	}

	// ── Phase 2: Send configuration via RPC ──
	if maxTimeUs > 0 {
		fmt.Fprintf(os.Stderr, "Sending configuration (trace=0x%02X, trigger_c=%d, max_c=%d, max_t=%.2fs)...\n",
			traceBitmask, triggerCycle, maxCycles, float64(maxTimeUs)/1e6)
	} else {
		fmt.Fprintf(os.Stderr, "Sending configuration (trace=0x%02X, trigger_c=%d, max_c=%d)...\n",
			traceBitmask, triggerCycle, maxCycles)
	}

	// Encode trigger and max values in config data (binary struct: 8B trig_c, 8B trig_t, 8B max_c, 8B max_t)
	configData := make([]byte, 32)
	binary.LittleEndian.PutUint64(configData[0:8], triggerCycle)
	binary.LittleEndian.PutUint64(configData[8:16], triggerTimeUs)
	binary.LittleEndian.PutUint64(configData[16:24], maxCycles)
	binary.LittleEndian.PutUint64(configData[24:32], maxTimeUs)

	resp, err := picoRpcCall("config", traceBitmask, int64(triggerCycle), int(triggerTimeUs), int64(maxCycles), configData)
	if err != nil || resp.Status != 0 {
		Fatalf("Config RPC failed: %v (status=%d %s)", err, resp.Status, resp.Message)
	}

	if *flagTuning != "" {
		fmt.Fprintf(os.Stderr, "Sending tuning parameters (%s)...\n", tuningParams.String())
		tuningData := tuningParams.Encode()
		resp, err = picoRpcCall("tuning", 0, 0, 0, 0, tuningData)
		if err != nil || resp.Status != 0 {
			Fatalf("Tuning RPC failed: %v (status=%d %s)", err, resp.Status, resp.Message)
		}
	}

	// ── Phase 3: Upload 65536-byte memory image in 1KB chunks ──
	fmt.Fprintf(os.Stderr, "Uploading 64KB memory image...\n")
	chunkSize := 1024
	for offset := 0; offset < 65536; offset += chunkSize {
		chunk := ramImage[offset : offset+chunkSize]
		resp, err := picoRpcCall("upload", 0, int64(offset), len(chunk), 0, chunk)
		if err != nil || resp.Status != 0 {
			Fatalf("Upload RPC failed at offset $%04X: %v (status=%d %s)", offset, err, resp.Status, resp.Message)
		}
		if (offset % 8192) == 0 {
			fmt.Fprintf(os.Stderr, "Uploaded %d / 65536 bytes...\n", offset+chunkSize)
		}
	}
	fmt.Fprintf(os.Stderr, "Upload complete.\n")

	// ── Phase 4: Start CPU ──
	fmt.Fprintf(os.Stderr, "Starting 6309 CPU...\n")
	resp, err = picoRpcCall("start", 0, 0, 0, 0, nil)
	if err != nil || resp.Status != 0 {
		Fatalf("Start RPC failed: %v (status=%d %s)", err, resp.Status, resp.Message)
	}
	cpuStarted.Store(true)
	speedEstimator.OnCycle(0)

	// ── Phase 5: Interactive Terminal ──
	if *flagN {
		// When -n is specified, there will never be any stdin.
		// Do not configure stty or run ReadLine; wait until process is terminated (e.g. by fault or signal).
		select {}
	}

	SetSttyCbreak()
	fmt.Fprintf(os.Stderr, "=== TurboLab Running (Type to send, ^C or enter line '^C' to send interrupt) ===\n")

	// Readline loop in foreground
	lineReader := NewLineReader(os.Stdin, os.Stdout)
	for {
		line, err := lineReader.ReadLine()
		if err != nil {
			break
		}
		// Send line over USB to Pico
		pkt := append([]byte{T_CONSOLE_LINE}, []byte(line)...)
		if line != "\x03" {
			pkt = append(pkt, '\r')
		}
		channelToPico <- pkt
	}

	speedEstimator.PrintReport()
	RestoreSttyState()
}
