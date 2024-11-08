package main

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/strickyak/gomar/sym"

	"github.com/strickyak/tfr9/v2/listings"
	"github.com/strickyak/tfr9/v2/os9api"

	"github.com/jacobsa/go-serial/serial"
)

var TTY = flag.String("tty", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")
var DISKS = flag.String("disks", "", "Comma-separated filepaths to disk files, in order of drive number")
var INPUT = flag.String("input", "", "force input text")

var CannedInputs = map[string]string{
	"1": `
echo Hello World
mdir
procs
free
dir
dir cmds
nando
`,
}

const (
	C_NOCHAR    = 160
	C_PUTCHAR   = 161
	C_GETCHAR   = 162
	C_STOP      = 163
	C_ABORT     = 164
	C_KEY       = 165
	C_NOKEY     = 166
	C_DUMP_RAM  = 167
	C_DUMP_LINE = 168
	C_DUMP_STOP = 169
	C_DUMP_PHYS = 170
	C_POKE      = 171
	C_EVENT     = 172
	EVENT_PC_M8 = 238
	EVENT_GIME  = 239
	EVENT_RTI   = 240
	EVENT_SWI2  = 241
	EVENT_CC    = 242
	EVENT_D     = 243
	EVENT_DP    = 244
	EVENT_X     = 245
	EVENT_Y     = 246
	EVENT_U     = 247
	EVENT_PC    = 248
	EVENT_SP    = 249
)

var CommandStrings = map[byte]string{
	160: "C_NOCHAR",
	161: "C_PUTCHAR",
	162: "C_GETCHAR",
	163: "C_STOP",
	164: "C_ABORT",
	165: "C_KEY",
	166: "C_NOKEY",
	167: "C_DUMP_RAM",
	168: "C_DUMP_LINE",
	169: "C_DUMP_STOP",
	170: "C_DUMP_PHYS",
	171: "C_POKE",
	172: "C_EVENT",
	238: "EVENT_PC_M8",
	239: "EVENT_GIME",
	240: "EVENT_RTI",
	241: "EVENT_SWI2",
	242: "EVENT_CC",
	243: "EVENT_D",
	244: "EVENT_DP",
	245: "EVENT_X",
	246: "EVENT_Y",
	247: "EVENT_U",
	248: "EVENT_PC",
	249: "EVENT_SP",
}

var NormalKeys = "@ABCDEFG" + "HIJKLMNO" + "PQRSTUVW" + "XYZ^\n\b\t " + "01234567" + "89:;,-./" + "\r\014\003"
var ShiftedKeys = "@abcdefg" + "hijklmno" + "pqrstuvw" + "xyz^\n\b\t " + "\177!\"#$%&'" + "()*+<=>?" + "\r\014\003"

// MatchFIC DEMO: @ fe9a 6e  =
var MatchFIC = regexp.MustCompile("^@@? ([0-9a-f]{4}) ([0-9a-f]{2})  =.*")

// plane: 0=no key 1=normal 2=shifted
func LookupCocoKey(ascii byte) (row, col, plane byte) {
	lenNK, lenSK := len(NormalKeys), len(ShiftedKeys)
	var ch byte
	for r := 0; r < 7; r++ {
		for c := 0; c < 8; c++ {
			i := r*8 + c
			if i < lenNK {
				ch = NormalKeys[i]
				if ch == ascii {
					row = (byte(1) << r)
					col = (byte(1) << c)
					return row, col, 1
				}
			}
			if i < lenSK {
				ch = ShiftedKeys[i]
				if ch == ascii {
					row = (byte(1) << r)
					col = (byte(1) << c)
					return row, col, 2
				}
			}
		}
	}
	return 0, 0, 0
}

type DiskFiles []*os.File

var CannedInput string

func InitInput(input string) {
	if x, ok := CannedInputs[input]; ok {
		CannedInput = x
	} else {
		CannedInput = input
	}
}

func OpenDisks(disks string) DiskFiles {
	var z DiskFiles
	for _, filename := range strings.Split(disks, ",") {
		if filename == "" {
			z = append(z, nil)
			continue
		}
		f, err := os.OpenFile(filename, os.O_RDWR, 0)
		if err != nil {
			log.Fatalf("Cannot open file %q: %v", filename, err)
		}
		z = append(z, f)
	}
	return z
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

func FormatReturn(os9num byte, call *os9api.Call, latest map[byte]*EventRec) string {
	var buf bytes.Buffer
	p := latest[EVENT_RTI].Peeks
	cc, rb := p[0], p[2]
	if (cc & 1) != 0 {
		errorName, _ := os9api.ErrorNames[rb]
		return Format("OS9_ERROR $%02x=%d. %q", rb, rb, errorName)
	}

	if call == nil {
		fmt.Fprintf(&buf, "( RD=%02x, RX=%02x, RY=%02x, RU=%02x", p[1:3], p[4:6], p[6:8], p[8:10])

	} else {
		fmt.Fprintf(&buf, "( ")
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
			if call.X[0] == '$' {
				fmt.Fprintf(&buf, "RX=%s=%02x=%q, ", call.X, p[4:6], FormatOs9Chars(latest[EVENT_X].Peeks))
			} else {
				fmt.Fprintf(&buf, "RX=%s=%02x, ", call.RX, p[4:6])
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
	return buf.String()
}

func FormatCall(os9num byte, call *os9api.Call, latest map[byte]*EventRec) string {
	var buf bytes.Buffer
	p := latest[EVENT_SWI2].Peeks

	if call == nil {
		fmt.Fprintf(&buf, "$%02x = UNKNOWN ( D=%02x, X=%02x, Y=%02x, U=%02x, ", os9num, p[1:3], p[4:6], p[6:8], p[8:10])

	} else {
		fmt.Fprintf(&buf, "$%02x = %s ( ", os9num, call.Name)
		if call.A != "" {
			fmt.Fprintf(&buf, "A=%s=%02x, ", call.A, p[1])
		}
		if call.B != "" {
			fmt.Fprintf(&buf, "B=%s=%02x, ", call.B, p[2])
		}
		if call.D != "" {
			fmt.Fprintf(&buf, "D=%s=%02x, ", call.D, p[1:3])
		}
		if call.X != "" {
			if call.X[0] == '$' {
				fmt.Fprintf(&buf, "X=%s=%02x=%q, ", call.X, p[4:6], FormatOs9Chars(latest[EVENT_X].Peeks))
			} else {
				fmt.Fprintf(&buf, "X=%s=%02x, ", call.X, p[4:6])
			}
		}
		if call.Y != "" {
			fmt.Fprintf(&buf, "Y=%s=%02x, ", call.Y, p[6:8])
		}
		if call.U != "" {
			fmt.Fprintf(&buf, "U=%s=%02x, ", call.U, p[8:10])
		}
	}

	mmapDesc := "No"
	gime := latest[EVENT_GIME].Peeks
	if (gime[0] & 0x40) != 0 {
		if (gime[1] & 1) != 0 {
			mmapDesc = fmt.Sprintf("T1: % 3x", gime[0x18:0x20])
		} else {
			mmapDesc = fmt.Sprintf("T0: % 3x", gime[0x10:0x18])
		}
	}
	fmt.Fprintf(&buf, ") %s", mmapDesc)
	return buf.String()
}

func Panicf(format string, args ...any) {
	// fmt.Printf("\n[[[ "+format+" ]]]\n", args...)
	log.Panicf("PANIC: "+format+"\n", args...)
}

// getByte from USB channel, for Binary Data
func getByte(fromUSB <-chan byte) byte {
	a := <-fromUSB
	return a
}

func WriteBytes(usbout chan []byte, vec ...byte) {
	usbout <- vec
}

var cr bool

const RAM_SIZE = 128 * 1024 // 128K
const RAM_MASK = RAM_SIZE - 1
const IO_PHYS = RAM_SIZE - 256

var trackRam [RAM_SIZE]byte

type EventRec struct {
	Number    byte
	Value     uint
	Peeks     []byte
	Call      string
	SerialNum uint
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

func Who() string {
	init1 := PPeek1(0x3ff91)
	mmuTask := init1 & 1
	if mmuTask == 0 {
		return ""
	}
	dProc := Peek2WithHalf(sym.D_Proc, 0)
	if dProc == 0 {
		return ""
	}
	procID := Peek1WithHalf(dProc+sym.P_ID, 0)
	pModul := Peek2WithHalf(dProc+sym.P_PModul, 0)
	mName := Peek2WithHalf(pModul+sym.M_Name, 1)
	s := pModul + mName

	// Log("dP=%x pM=%x mN=%x s=%x", dProc, pModul, mName, s)

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "%x:", procID)
	for {
		ch := Peek1WithHalf(s, 1)
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

func Run(files DiskFiles, inkey chan byte) {
    var remember int64
    var timer_sum int64
    var timer_count int64

	// Set up options for Serial Port.
	options := serial.OpenOptions{
		PortName:        *TTY,
		BaudRate:        *BAUD,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

	pendingKeyedEvents := make(map[string]map[byte]*EventRec)
	latestEventOfType := make(map[byte]*EventRec)

	// Open the Serial Port.
	port, err := serial.Open(options)
	if err != nil {
		Panicf("serial.Open: %v", err)
	}

	// Make sure to close it later.
	defer port.Close()

	usbout := make(chan []byte, 1024)

	go ToUsbRoutine(port, usbout)

	viaUSB := make(chan byte, 1024)
	var fromUSB <-chan byte = viaUSB

	go func() {
		var serialNumCounter uint
		var mintSerialNum = func() uint {
			serialNumCounter++
			return serialNumCounter
		}
		var bb bytes.Buffer

		pushBB := func() { // Flush the bytes.Buffer to log.Printf
			// DEMO: @ fe9a 6e  =
			s := bb.String()
			if strings.HasSuffix(s, "\r") {
				s = s[:len(s)-1]
			}
			m := MatchFIC.FindStringSubmatch(s)
			if m != nil {
				addr, _ := strconv.ParseUint(m[1], 16, 64)
				// data, _ := strconv.ParseUint(m[2], 16, 64)
				phys := Physical(uint(addr))
				modName, modOffset := MemoryModuleOf(phys)
				s += fmt.Sprintf(" %s:%06x :: %q+%04x %s", CurrentHardwareMMap(), phys, modName, modOffset, AsmSourceLine(modName, modOffset))
			}

			log.Printf("# %s", s)
			bb.Reset()
		}

		stop := false
		gap := 1
		//FOR:
		for {

			inchar, incharOK := TryInkey(inkey)
			if incharOK {
				// WriteBytes(port, inchar)
				WriteBytes(usbout, inchar)
				// time.Sleep(100 * time.Millisecond)
			}

			var cmd byte = getByte(fromUSB)
			bogus := 0

			switch cmd {
			case C_EVENT:
				event := getByte(fromUSB)
				sz := getByte(fromUSB)
				hi := getByte(fromUSB)
				lo := getByte(fromUSB)
				value := (uint(hi) << 8) | uint(lo)
				vec := make([]byte, sz)
				for i := byte(0); i < sz; i++ {
					vec[i] = getByte(fromUSB)
				}
				eventName, ok := CommandStrings[event]
				if !ok {
					log.Fatalf("Unknown event number: %d.", event)
				}
				log.Printf("C_EVENT %q $%04x: % 3x %q", eventName, value, vec, FormatOs9Chars(vec))

				//for i := byte(0); i < sz; i++ {
				//x := Peek1(uint(i) + value)
				//if vec[i] != x {
				//log.Printf("C_EVENT BAD VEC @ %x: %x vs %x", i, vec[i], x)
				//}
				//}

				eRec := &EventRec{
					Number: event,
					Value:  value,
					Peeks:  vec,
				}
				latestEventOfType[event] = eRec

				if event == EVENT_SWI2 {
					latestPC, ok := latestEventOfType[EVENT_PC]
					if ok && len(latestPC.Peeks) > 1 {
						os9num := latestPC.Peeks[0]

						call, _ := os9api.CallOf[os9num]
						eRec.SerialNum = mintSerialNum()
						eRec.Call = FormatCall(os9num, call, latestEventOfType)

						lastPC := latestPC.Value
						key := Format("os9_%02x_%04x_%04x_%s", os9num, lastPC, value, CurrentHardwareMMap()[:2])
						log.Printf("\n%q === OS9_CALL _%d_:  %s\n\n", Who(), eRec.SerialNum, eRec.Call)

						pendingKeyedEvents[key] = latestEventOfType
						latestEventOfType = make(map[byte]*EventRec) // clear before next big event
					} else {
						log.Printf("--- BAD latestPC in SWi2 ---")
					}
				}
				if event == EVENT_RTI {
					if latestPC_M8, ok := latestEventOfType[EVENT_PC_M8]; ok {
						if p := latestPC_M8.Peeks; len(p) >= 8 {
							lastPC := 8 + latestPC_M8.Value

							os9num := byte(255) // tentatively, a non-existant number.
							key := ""           // tentatively

							if p[8-3] == 0x10 && p[8-2] == 0x3F {
								// Previous instruction appears to have been a SWI2 and a system call number.
								// (Note this might be wrong if an IRQ occurs at just the wrong time.)
								os9num = p[8-1]
								// The matching SWI2 event would have the PC at the system call number.
								lastPC -= 1
								key = Format("os9_%02x_%04x_%04x_%s", os9num, lastPC, value, CurrentHardwareMMap()[:2])
							}

							// Log("lastPC=%04x , key=%q, p = % 3x ; latestPC_M8=%#v", lastPC, key, p, latestPC_M8)

							if pending, ok := pendingKeyedEvents[key]; ok {
								// Log("Found pending keyed event %q: %#v", key, pending)
								if swi2Event, ok := pending[EVENT_SWI2]; ok {

									call, _ := os9api.CallOf[os9num]
									returnString := FormatReturn(os9num, call, latestEventOfType)
									Log("\n%q === OS9_RETURN _%d_:  %s %s\n\n", Who(), swi2Event.SerialNum, swi2Event.Call, returnString)
								}
								delete(pendingKeyedEvents, key)
							} else {
								Log("NOT FOUND: pending keyed event %q", key)
							}

						} else {
							Log("len(p) FAILS")
						}
					} else {
						Log("PC_M8 FAILS")
					}

					latestEventOfType = make(map[byte]*EventRec) // clear before next big event

					DumpRam()
				}

			case C_POKE:
				hi := getByte(fromUSB)
				mid := getByte(fromUSB)
				lo := getByte(fromUSB)
				data := getByte(fromUSB)
				longaddr := (uint(hi) << 16) | (uint(mid) << 8) | uint(lo)
				longaddr &= RAM_MASK

				log.Printf("       =C_POKE= %06x %02x (was %02x)", longaddr, data, trackRam[longaddr])
				trackRam[longaddr] = data

			case C_DUMP_RAM, C_DUMP_PHYS:
				log.Printf("{{{ %s", CommandStrings[cmd])
			DUMPING:
				for {
					what := getByte(fromUSB)
					switch what {
					case C_DUMP_LINE:
						a := getByte(fromUSB)
						b := getByte(fromUSB)
						c := getByte(fromUSB)
						var d [16]byte
						for j := uint(0); j < 16; j++ {
							d[j] = getByte(fromUSB)
						}

						if cmd == C_DUMP_PHYS {
							for j := uint(0); j < 16; j++ {
								longaddr := (uint(a)<<16 | uint(b)<<8 | uint(c)) + j
								if d[j] != trackRam[longaddr] {
									log.Printf("--- WRONG PHYS %06x ( %02x vs %02x ) ---", longaddr, d[j], trackRam[longaddr])
								}
							}
						}

						var buf bytes.Buffer
						fmt.Fprintf(&buf, ":%06x: ", (uint(a)<<16 | uint(b)<<8 | uint(c)))
						for j := 0; j < 16; j++ {
							fmt.Fprintf(&buf, "%02x ", d[j])
							if j == 7 {
								buf.WriteByte(' ')
							}
						}
						buf.WriteByte('|')
						for j := 0; j < 16; j++ {
							ch := d[j]
							if ch > 127 {
								ch = '#'
							} else {
								ch = ch & 63
								if ch < 32 {
									ch += 64
								}
								if ch == 64 {
									ch = '.'
								}
							}
							buf.WriteByte(ch)
						}
						buf.WriteByte('|')
						log.Printf("%s", buf.String())
						break

					case C_DUMP_STOP:
						break DUMPING
					default:
						log.Printf("FUNNY CHAR: %d.", what)
						bogus++
						if bogus > 10 {
							bogus = 0
							break DUMPING
						}
					}
				}
				log.Printf("}}} %s", CommandStrings[cmd])

			case C_PUTCHAR:
				ch := getByte(fromUSB)
				switch {
				case 32 <= ch && ch <= 126:
					fmt.Printf("%c", ch)
					cr = false
                    if (ch == '{') {
                        remember = time.Now().UnixMicro()
                    }
                    if (ch == '}') {
                        now := time.Now().UnixMicro()
                        micros := now - remember
                        fmt.Printf("[%.6f : ", float64(micros) / 1000000.0 )
                        timer_sum += micros
                        timer_count++
                        fmt.Printf(" %.6f]", float64(timer_sum) / 1000000.0 / float64(timer_count) )
                    }
				case ch == 13:
					fmt.Println()
					cr = true
				case ch == 10:
					if !cr {
						fmt.Println() // lf skips Println after cr does Println
					}
					cr = false
				case ch == 255:
					log.Fatalf("FATAL BECAUSE PUTCHAR 255")
				default:
					fmt.Printf("{%d}", ch)
					cr = false
				}
				if false {
					token := "."
					if cr {
						token = "+"
					}
					fmt.Printf("[%d%s]", ch, token)
				}

			case C_KEY:
				if stop {
					WriteBytes(usbout, C_STOP)
				} else {
					if gap > 0 {
						gap--
						WriteBytes(usbout, C_NOKEY)
					} else {
						b1 := make([]byte, 1)
						sz, err := os.Stdin.Read(b1)
						if err != nil {
							log.Panicf("cannot os.Stdin.Read: %v", err)
						}
						if sz == 1 {
							x := b1[0]
							if x == 10 { // if LF
								x = 13 // use CR
							}

							row, col, plane := LookupCocoKey(x)

							WriteBytes(usbout, C_KEY, row, col, plane)
						} else {
							WriteBytes(usbout, C_NOKEY)
						}
					}
				}

			case C_GETCHAR:
				log.Fatalf("NO MORE GETCHAR")
				//				if stop {
				//					WriteBytes(port, C_GETCHAR, C_STOP)
				//                    log.Fatalf("STOPPING")
				//                    /*
				//				} else if len(CannedInput) > 0 {
				//					WriteBytes(port, C_GETCHAR, CannedInput[0])
				//					CannedInput = CannedInput[1:]
				//					if len(CannedInput) == 0 {
				//						stop = true
				//					}
				//                    */
				//				} else {
				//                    character, ok := TryInkey(inkey)
				//                    if ok {
				//						WriteBytes(port, C_GETCHAR, character)
				//					} else {
				//						WriteBytes(port, C_NOCHAR)
				//					}
				//				}

			case 255:
				fmt.Printf("\n[255: finished]\n")
				log.Printf("go func: Received END MARK 255; exiting")
				close(viaUSB)
				log.Fatalf("go func: Received END MARK 255; exiting")
				return

			default:
				switch {
				case 32 <= cmd && cmd <= 126:
					bb.WriteByte(cmd)
				case cmd == 13:
					// do nothing
					bb.WriteByte(cmd)
					//> log.Printf("# %s", bb.String())
					//> bb.Reset()
					pushBB()
				case cmd == 10:
					// fmt.Fprintf(&bb, "{%d}", cmd)
				default:
					fmt.Fprintf(&bb, "{%d}", cmd)
				}
			}
			if bb.Len() > 250 {
				//> log.Printf("# %q\\", bb.String())
				//> bb.Reset()
				pushBB()
			}
		}
	}()

	for {
		v := make([]byte, 1024)
		n, err := port.Read(v)
		if err != nil {
			Panicf("port.Read: %v", err)
		}

		for i := 0; i < n; i++ {
			viaUSB <- v[i]
		}
	}
	log.Printf("End LOOP.")
}
func TryRun(files DiskFiles, inkey chan byte) {
	defer func() {
		r := recover()
		if r != nil {
			fmt.Printf("[recover: %q]\n", r)
		}
	}()
	Run(files, inkey)
}
func main() {
	log.SetFlags(0)
	// log.SetPrefix("!")
	flag.Parse()

	if false { // Why doesnt this seem to have the effect
		sttyErr := exec.Command("stty", "cbreak", "min", "1").Run()
		if sttyErr != nil {
			log.Printf("stty failed: %v", sttyErr)
		}

		// HINT FROM https://github.com/SimonWaldherr/golang-minigames/blob/master/snake.go
		// exec.Command("stty", "cbreak", "min", "1").Run()
		// exec.Command("stty", "-f", "/dev/tty", "-echo").Run()
		// exec.Command("stty", "-echo").Run()
	}

	inkey := make(chan byte, 1024)
	go InkeyRoutine(inkey)

	killed := make(chan os.Signal)
	signal.Notify(killed, syscall.SIGINT)
	go func() {
		sig := <-killed
		log.Printf("\n*** STOPPING ON SIGNAL %q", sig)
		os.Exit(3)
	}()

	InitInput(*INPUT)

	files := OpenDisks(*DISKS)
	for {
		TryRun(files, inkey)
		time.Sleep(1 * time.Second)
	}
}

func InkeyRoutine(inkey chan byte) {
	for {
		bb := make([]byte, 1)
		sz, err := os.Stdin.Read(bb)
		if err != nil {
			log.Panicf("cannot os.Stdin.Read: %v", err)
		}
		if sz == 1 {
			inkey <- bb[0]
		}
	}
}

func TryInkey(inkey chan byte) (byte, bool) {
	select {
	case x := <-inkey:
		return x, true
	default:
		return 0, false
	}
}

func ToUsbRoutine(w io.Writer, usbout chan []byte) {
	for bb := range usbout {
		_, err := w.Write(bb)
		if err != nil {
			log.Printf("ToUsb: %v", err)
		}
		// for i:=0; i < len(bb); i++ {
		// w.Write(bb[i:i+1])
		// // time.Sleep(1 * time.Millisecond)  // was 50ms
		// }
	}
}

type Mapping [8]uint

func GetMappingFromTable(addr uint) Mapping {
	// Mappings are always in block 0?
	return Mapping{
		// TODO: drop the "0x3F &".
		0x3F & PPeek2(addr),
		0x3F & PPeek2(addr+2),
		0x3F & PPeek2(addr+4),
		0x3F & PPeek2(addr+6),
		0x3F & PPeek2(addr+8),
		0x3F & PPeek2(addr+10),
		0x3F & PPeek2(addr+12),
		0x3F & PPeek2(addr+14),
	}
}

func Peek1(addr uint) byte {
	phys := Physical(addr)
	return trackRam[phys&RAM_MASK]
}
func Peek2(addr uint) uint {
	hi := Peek1(addr)
	lo := Peek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}

func Physical(logical uint) uint {
	block := byte(0x3F) // tentative
	low13 := uint(logical & 0x1FFF)

	if logical < 0xFFE0 { // must compute block
		mapHW := uint(0x3FFA0) // Task 0 map hardware
		if (PPeek1(0x3FF91) & 1) != 0 {
			mapHW += 8 // task 1 map hardware
		}
		//log.Printf("mapHW: %x %x %x %x %x %x %x %x",
		//PPeek1(mapHW+0),
		//PPeek1(mapHW+1),
		//PPeek1(mapHW+2),
		//PPeek1(mapHW+3),
		//PPeek1(mapHW+4),
		//PPeek1(mapHW+5),
		//PPeek1(mapHW+6),
		//PPeek1(mapHW+7))

		slot := 7 & (logical >> 13)
		block = PPeek1(mapHW + slot)
		//log.Printf("Physical: a=%x b=%x p=%x", logical, block, (uint(block) << 13) | low13)
	}

	return (uint(block) << 13) | low13
}

func MapAddrWithMapping(logical uint, m Mapping) uint {
	slot := 7 & (logical >> 13)
	low13 := uint(logical & 0x1FFF)
	physicalPage := m[slot]
	return (uint(physicalPage) << 13) | low13
}

func HalfNumberToMapping(half byte) Mapping {
	base := IO_PHYS + 0xA0 + (8 * uint(half))
	var m Mapping
	for i := uint(0); i < 8; i++ {
		m[i] = uint(PPeek1(base + i))
	}
	// Log("Half %x %x => %x", half, base, m)
	return m
}
func Peek1WithHalf(addr uint, half byte) byte {
	m := HalfNumberToMapping(half)
	return Peek1WithMapping(addr, m)
}
func Peek2WithHalf(addr uint, half byte) uint {
	m := HalfNumberToMapping(half)
	return Peek2WithMapping(addr, m)
}
func Peek1WithMapping(addr uint, m Mapping) byte {
	logBlock := (addr >> 13) & 7
	physBlock := m[logBlock]
	if addr >= 0xFF00 {
		physBlock = 0x3F
	} else if addr >= 0xFE00 { // TODO: check FExx bit.
		physBlock = 0x3F
	}
	ptr := (addr & 0x1FFF) | (uint(physBlock) << 13)
	return PPeek1(ptr)
}
func Peek2WithMapping(addr uint, m Mapping) uint {
	hi := Peek1WithMapping(addr, m)
	lo := Peek1WithMapping(addr+1, m)
	return (uint(hi) << 8) | uint(lo)
}
func MemoryModuleOf(addrPhys uint) (name string, offset uint) {
	beginDir, endDir := PPeek2(sym.D_ModDir), PPeek2(sym.D_ModEnd)

	datPtr0 := PPeek2(beginDir)
	if datPtr0 == 0 {
		var p uint
		var z uint

		ppeek1 := func(a uint) byte {
			z := PPeek1(a)
			// log.Printf("ppeek1: %x -> %x", a, z)
			return z
		}

		ppeek2 := func(a uint) uint {
			z := PPeek2(a)
			// log.Printf("ppeek2: %x -> %x", a, z)
			return z
		}

		if 0x072600 <= addrPhys && addrPhys <= 0x074000 {
			p = addrPhys - 0x072600 + 0x0D00
			z = 0x072600 - 0x0D00
		} else if true {
			p = addrPhys
			z = 0
		}

		p = (p & 0x1fff)

		if 0x0D06 <= p && p < 0x0E30 {
			sz := ppeek2(z + 0x0D06 + 2)
			a, b, c := ppeek1(z+0x0D06+sz-3), ppeek1(z+0x0D06+sz-2), ppeek1(z+0x0D06+sz-1)
			log.Printf("MMOf/rel: %x -> %x", addrPhys, p)
			return fmt.Sprintf("rel.%04x%02x%02x%02x", sz, a, b, c), p - 0x0D06
		} else if 0x0E30 <= p && p < 0x1000 {
			sz := ppeek2(z + 0x0E30 + 2)
			a, b, c := ppeek1(z+0x0E30+sz-3), ppeek1(z+0x0E30+sz-2), ppeek1(z+0x0E30+sz-1)
			log.Printf("MMOf/boot: %x -> %x", addrPhys, p)
			return fmt.Sprintf("boot.%04x%02x%02x%02x", sz, a, b, c), p - 0x0E30
		} else if 0x1000 <= p && p < 0x1F00 {
			sz := ppeek2(z + 0x1000 + 2)
			a, b, c := ppeek1(z+0x1000+sz-3), ppeek1(z+0x1000+sz-2), ppeek1(z+0x1000+sz-1)
			log.Printf("MMOf/krn: %x -> %x", addrPhys, p)
			return fmt.Sprintf("krn.%04x%02x%02x%02x", sz, a, b, c), p - 0x1000
		} else {
			return "=0=", p
		}

		/*
		   if 0xED06 <= p && p < 0xEE30 {
		       sz := PPeek2(0xED08)
		       a, b, c := PPeek1(0xED06+sz-3), PPeek1(0xED06+sz-2), PPeek1(0xED06+sz-1)
		       return fmt.Sprintf("rel.%04x%02x%02x%02x", sz, a, b, c), p - 0xED06
		   } else if 0xEE30 <= p && p < 0xF000 {
		       sz := PPeek2(0xEE32)
		       a, b, c := PPeek1(0xEE30+sz-3), PPeek1(0xEE30+sz-2), PPeek1(0xEE30+sz-1)
		       return fmt.Sprintf("rel.%04x%02x%02x%02x", sz, a, b, c), p - 0xEE30
		   } else if 0xF000 <= p && p < 0xFF00 {
		       sz := PPeek2(0xF002)
		       a, b, c := PPeek1(0xF000+sz-3), PPeek1(0xF000+sz-2), PPeek1(0xF000+sz-1)
		       return fmt.Sprintf("rel.%04x%02x%02x%02x", sz, a, b, c), p - 0xF000
		   } else {
		       return "=0=", addrPhys & 0x1fff
		   }
		*/
		// NOT REACHED
	}

	if beginDir != 0 && endDir != 0 {
		for i := beginDir; i < endDir; i += 8 {
			datPtr := PPeek2(i)
			if datPtr == 0 {
				continue
			}
			mapping := GetMappingFromTable(datPtr)

			begin := PPeek2(i + 4)
			//if begin == 0 {
			//continue
			//}

			magic := Peek2WithMapping(begin, mapping)
			if magic != 0x87CD {
				return "=m=", addrPhys
			}
			// log.Printf("DDT: magic i=%x datPtr=%x begin=%x mapping=% 03x", i, datPtr, begin, mapping)

			modSize := Peek2WithMapping(begin+2, mapping)
			//modNamePtr := Peek2WithMapping(begin+4, mapping)
			//_ = modNamePtr
			//links := Peek2WithMapping(begin+6, mapping)

			remaining := modSize
			region := begin
			offset := uint(0)
			for remaining > 0 {
				// If module crosses paged blocks, it has more than one region.
				regionP := MapAddrWithMapping(region, mapping)
				endOfRegionBlockP := 1 + (regionP | 0x1FFF)
				regionSize := remaining
				if regionSize > endOfRegionBlockP-regionP {
					// A smaller region of the module.
					regionSize = endOfRegionBlockP - regionP
				}

				// log.Printf("DDT: try regionP=%x (phys=%x) regionEnds=%x remain=%x", regionP, addrPhys, regionP+regionSize, remaining)
				if regionP <= addrPhys && addrPhys < regionP+regionSize {
					//if links == 0 {
					// return "unlinkedMod", addrPhys
					// log.Panicf("in unlinked module: i=%x addrPhys=%x", i, addrPhys)
					//}
					id := ModuleId(begin, mapping)
					delta := offset + (addrPhys - regionP)
					// log.Printf("DDT: [links=%x] FOUND %q+%x", links, id, delta)
					return id, delta
				}
				remaining -= regionSize
				regionP += regionSize
				region += uint(regionSize)
				offset += uint(regionSize)
				// log.Printf("DDT: advanced remaining=%x regionSize=%x", remaining, regionSize)
			}

		}
	}
	return "==", addrPhys
}
func ModuleId(begin uint, m Mapping) string {
	namePtr := begin + Peek2WithMapping(begin+4, m)
	modname := strings.ToLower(Os9StringWithMapping(namePtr, m))
	sz := Peek2WithMapping(begin+2, m)
	crc1 := Peek1WithMapping(begin+sz-3, m)
	crc2 := Peek1WithMapping(begin+sz-2, m)
	crc3 := Peek1WithMapping(begin+sz-1, m)
	return fmt.Sprintf("%s.%04x%02x%02x%02x", modname, sz, crc1, crc2, crc3)
}

func Os9StringWithMapping(addr uint, m Mapping) string {
	var buf bytes.Buffer
	for {
		var b byte = Peek1WithMapping(addr, m)
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

func CurrentHardwareMMap() string {
	init0, init1 := PPeek1(0x3ff90), PPeek1(0x3ff91)
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
		PPeek1(mapHW+0),
		PPeek1(mapHW+1),
		PPeek1(mapHW+2),
		PPeek1(mapHW+3),
		PPeek1(mapHW+4),
		PPeek1(mapHW+5),
		PPeek1(mapHW+6),
		PPeek1(mapHW+7))
}

var Sources = make(map[string]*listings.ModSrc)

func AsmSourceLine(modName string, offset uint) string {
	modsrc, ok := Sources[modName]
	if !ok {
		modsrc = listings.LoadFile(*listings.Borges + "/" + modName)
		Sources[modName] = modsrc
	}
	if modsrc == nil {
		Sources[modName] = &listings.ModSrc{
			Src:      make(map[uint]string),
			Filename: modName,
		}
		return ""
	}
	srcLine, _ := modsrc.Src[offset]
	return srcLine
}

func DumpRam() {
	Log("DumpRam (((((")
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
		Log(buf.String())
	}
	Log("DumpRam )))))")
}

var Format = fmt.Sprintf
var Log = log.Printf
