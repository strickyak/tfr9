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
	"runtime"
	// "strconv"
	// "strings"
	"syscall"
	"time"
	// "github.com/strickyak/gomar/sym"
	// "github.com/strickyak/tfr9/v2/listings"
	// "github.com/strickyak/tfr9/v2/os9api"
	// "github.com/jacobsa/go-serial/serial"
)

var WIRE = flag.String("wire", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")
var DISKS = flag.String("disks", "/home/strick/coco-shelf/tfr9/v2/generated/level2.disk", "Comma-separated filepaths to disk files, in order of drive number")

const (
	C_LOGGING    = 130 // Ten levels, 130 to 139
	C_CYCLE      = 160 // one machine cycle
	C_PUTCHAR    = 161
	C_STOP       = 163
	C_ABORT      = 164
	C_KEY        = 165
	C_NOKEY      = 166
	C_DUMP_RAM   = 167
	C_DUMP_LINE  = 168
	C_DUMP_STOP  = 169
	C_DUMP_PHYS  = 170
	C_WRITING    = 171
	C_EVENT      = 172
	C_DISK_READ  = 173
	C_DISK_WRITE = 174
	C_CONFIG     = 175
	// EVENT_PC_M8  = 238
	// EVENT_GIME   = 239
	EVENT_RTI  = 240
	EVENT_SWI2 = 241
	/*
		EVENT_CC   = 242
		EVENT_D    = 243
		EVENT_DP   = 244
		EVENT_X    = 245
		EVENT_Y    = 246
		EVENT_U    = 247
		EVENT_PC   = 248
		EVENT_SP   = 249
	*/
)

var CommandStrings = map[byte]string{
	130: "C_LOGGING_0",
	131: "C_LOGGING_1",
	132: "C_LOGGING_2",
	133: "C_LOGGING_3",
	134: "C_LOGGING_4",
	135: "C_LOGGING_5",
	136: "C_LOGGING_6",
	137: "C_LOGGING_7",
	138: "C_LOGGING_8",
	139: "C_LOGGING_9",
	161: "C_PUTCHAR",
	163: "C_STOP",
	164: "C_ABORT",
	165: "C_KEY",
	166: "C_NOKEY",
	167: "C_DUMP_RAM",
	168: "C_DUMP_LINE",
	169: "C_DUMP_STOP",
	170: "C_DUMP_PHYS",
	171: "C_WRITING",
	172: "C_EVENT",
	// 238: "EVENT_PC_M8",
	// 239: "EVENT_GIME",
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

/*
var ConfigStr string
*/

var LastSerialNumber uint

func MintSerial() uint {
	LastSerialNumber++
	return LastSerialNumber
}

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

// getByte from USB channel, for Binary Data
func getByte(fromUSB <-chan byte) byte {
	return <-fromUSB
}

func WriteBytes(channelToPico chan []byte, vec ...byte) {
	channelToPico <- vec
}

var cr bool

type EventRec struct {
	Number    byte
	PC        uint
	Cycle     uint
	Os9Num    byte
	Datas     []byte
	Addrs     []uint
	Call      string
	SerialNum uint
}

func TryRun(inkey chan byte) {
	defer func() {
		r := recover()
		if r != nil {
			fmt.Printf("[recover: %q]\n", r)
		}
	}()
	Run(inkey)
}

func SttyCbreakMode() {
	sttyPath, err := exec.LookPath("stty")
	if err != nil {
		log.Fatalf("Cannot find stty: %v", err)
	}
	cmd := &exec.Cmd{
		Path: sttyPath,
		// Args: []string{"stty", "cbreak", "-echo", "min", "1"},
		Args:   []string{"stty", "cbreak", "-echo"},
		Stdin:  os.Stdin,
		Stdout: os.Stdout,
		Stderr: os.Stderr,
	}
	err = cmd.Run()
	if err != nil {
		log.Fatalf("Cannot run stty: %v", err)
	}
}

func main() {
	log.SetFlags(0)
	flag.Parse()
	InstallLimitedLogWriter()
	if runtime.GOOS != "windows" {
		SttyCbreakMode()
	}

	if false { // Why doesnt this seem to have the effect
		sttyErr := exec.Command("stty", "cbreak", "min", "-echo", "1").Run()
		if sttyErr != nil {
			Logf("stty failed: %v", sttyErr)
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
		Logf("\n*** STOPPING ON SIGNAL %q", sig)
		os.Exit(3)
	}()

	OpenDisks(*DISKS)
	for {
		TryRun(inkey)
		time.Sleep(1 * time.Second)
	}
}

func InkeyRoutine(inkey chan byte) {
	defer func() {
		r := recover()
		if r != nil {
			Logf("InkeyRoutine: recovers panic: %v", r)
		}
	}()
	for {
		bb := make([]byte, 1)
		Logf("InkeyRoutine: reading...")
		sz, err := os.Stdin.Read(bb)
		Logf("InkeyRoutine:    ... sz=%d err=%v", sz, err)
		if err != nil {
			Panicf("cannot os.Stdin.Read: %v", err)
		}
		Logf("INKEY:%d(%q)", bb[0], bb)
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

func ToUsbRoutine(w io.Writer, channelToPico chan []byte) {
	for bb := range channelToPico {
		_, err := w.Write(bb)
		if err != nil {
			Logf("ToUsb: %v", err)
			return
		}
	}
}

var serialNumCounter uint

func MintSerialNum() uint {
	serialNumCounter++
	return serialNumCounter
}

func Run(inkey chan byte) {
	const SERIAL_BUFFER_SIZE = 1024

	var previousPutChar byte
	var remember int64
	var timer_sum int64
	var timer_count int64

	// Set up options for Serial Port.
	options := /*serial.*/ OpenOptions{
		PortName:        *WIRE,
		BaudRate:        *BAUD,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

	pending := make(map[string]*EventRec)

	// Open the Serial Port.
	serialPort, err := /*serial.*/ Open(options)
	if err != nil {
		Panicf("serial.Open: %v", err)
	}

	// Make sure to close it later.
	defer serialPort.Close()

	channelToPico := make(chan []byte, 1024)

	go ToUsbRoutine(serialPort, channelToPico)

	channelFromPico := make(chan byte, SERIAL_BUFFER_SIZE)
	var fromUSB <-chan byte = channelFromPico

	go func() {
		/*
			var bb bytes.Buffer

			pushBB := func() { // Flush the bytes.Buffer to Logf
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

				Logf("# %s", s)
				bb.Reset()
			}
		*/
		gap := 1
		//FOR:
		for {
			select {
			case inchar := <-inkey:
				WriteBytes(channelToPico, inchar)

			case cmd := <-fromUSB:
				//X// Logf("@@ fromUSB @@ $%x=%d.", cmd, cmd)

				bogus := 0

				var ch byte // Used by default and C_PUTCHAR

				switch cmd {

				case 0:
					// Ignored.
					Logf("ZERO")

				case C_CYCLE:
					sz := GetSize(fromUSB)
					if sz == 8 {
						b := make([]byte, sz)
						for i := uint(0); i < sz; i++ {
							b[i] = getByte(fromUSB)
						}
						_cy := (uint(b[0]) << 24) + (uint(b[1]) << 16) + (uint(b[2]) << 8) + uint(b[3])
						_fl := b[4] & 31
						_kind := b[4] >> 5
						_data := b[5]
						_addr := (uint(b[6]) << 8) + uint(b[7])

						var s string
						if _kind == CY_IDLE {
							s = Format("cy - ---- -- =%x #%d", _fl, _cy)
						} else {
							s = Format("cy %s %04x %02x =%x #%d", CycleKindStr[_kind], _addr, _data, _fl, _cy)
						}

						phys := Physical(uint(_addr))
						modName, modOffset := MemoryModuleOf(phys)
						//  s += fmt.Sprintf(" %s:%06x :: %q+%04x %s", CurrentHardwareMMap(), phys, modName, modOffset, AsmSourceLine(modName, modOffset))
						Logf("%s %s:%06x :: %q+%04x %s", s, CurrentHardwareMMap(), phys, modName, modOffset, AsmSourceLine(modName, modOffset))
					}

				case C_LOGGING,
					C_LOGGING + 1,
					C_LOGGING + 2,
					C_LOGGING + 3,
					C_LOGGING + 4,
					C_LOGGING + 5,
					C_LOGGING + 6,
					C_LOGGING + 7,
					C_LOGGING + 8,
					C_LOGGING + 9:
					sz := GetSize(fromUSB)
					buf := make([]byte, sz)
					for i := uint(0); i < sz; i++ {
						buf[i] = getByte(fromUSB)
					}
					Logf("LOG[%d]: %q", cmd-C_LOGGING, buf)

				/*
					case C_CONFIG:
						sz := <-fromUSB
						for i := 0; i < int(sz); i++ {
							ConfigStr += string([]byte{<-fromUSB})
						}
						Logf("ConfigStr: %q", ConfigStr)
				*/

				case C_DISK_WRITE:
					EmulateDiskWrite(fromUSB, channelToPico)

				case C_DISK_READ:
					EmulateDiskRead(fromUSB, channelToPico)

				case C_EVENT:
					OnEvent(fromUSB, pending)

				case C_WRITING:
					hi := getByte(fromUSB)
					mid := getByte(fromUSB)
					lo := getByte(fromUSB)
					data := getByte(fromUSB)
					longaddr := (uint(hi) << 16) | (uint(mid) << 8) | uint(lo)
					longaddr &= RAM_MASK

					//X// Logf("       =C_WRITING= %06x %02x (was %02x)", longaddr, data, trackRam[longaddr])
					trackRam[longaddr] = data

					if IO_PHYS <= longaddr && longaddr <= IO_PHYS+255 {
						HandleIOPoke(longaddr, data)
					}

				case C_DUMP_RAM, C_DUMP_PHYS:
					Logf("{{{ %s", CommandStrings[cmd])
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
									longaddr %= RAM_SIZE
									if d[j] != trackRam[longaddr] {
										Logf("--- WRONG PHYS %06x ( %02x vs %02x ) ---", longaddr, d[j], trackRam[longaddr])
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
								r := d[j]
								if r > 127 {
									r = '#'
								} else {
									r = r & 63
									if r < 32 {
										r += 64
									}
									if r == 64 {
										r = '.'
									}
								}
								buf.WriteByte(r)
							}
							buf.WriteByte('|')
							Logf("%s", buf.String())
							break

						case C_DUMP_STOP:
							break DUMPING
						default:
							Logf("FUNNY CHAR: %d.", what)
							bogus++
							if bogus > 10 {
								bogus = 0
								break DUMPING
							}
						}
					}
					Logf("}}} %s", CommandStrings[cmd])

				default:
					ch = cmd
					fallthrough

				case C_PUTCHAR:
					if cmd == C_PUTCHAR {
						ch = getByte(fromUSB)
					} // otherwise ue the ch from default case.

					switch {
					case 32 <= ch && ch <= 126:
						fmt.Printf("%c", ch)
						cr = false
						if ch == '{' && previousPutChar == '^' {
							remember = time.Now().UnixMicro()
						}
						if ch == '@' {
							timer_sum, timer_count = 0, 0
						}
						if ch == '}' && previousPutChar == '^' {
							now := time.Now().UnixMicro()
							micros := now - remember
							fmt.Printf("[%.6f : ", float64(micros)/1000000.0)
							timer_sum += micros
							timer_count++
							fmt.Printf("%d :  %.6f]", timer_count, float64(timer_sum)/1000000.0/float64(timer_count))
						}
					case ch == 10 || ch == 13:
						if previousPutChar == 10 || previousPutChar == 13 {
							// skip extra newline
						} else {
							fmt.Println() // lf skips Println after cr does Println
						}

					default:
						fmt.Printf("{%d}", ch)
						cr = false
					} // end inner switch on ch range
					previousPutChar = ch

				case C_KEY:
					{
						if gap > 0 {
							gap--
							WriteBytes(channelToPico, C_NOKEY)
						} else {
							b1 := make([]byte, 1)
							sz, err := os.Stdin.Read(b1)
							if err != nil {
								Panicf("cannot os.Stdin.Read: %v", err)
							}
							if sz == 1 {
								x := b1[0]
								if x == 10 { // if LF
									x = 13 // use CR
								}

								row, col, plane := LookupCocoKey(x)

								WriteBytes(channelToPico, C_KEY, row, col, plane)
							} else {
								WriteBytes(channelToPico, C_NOKEY)
							}
						}
					}

				case 255:
					fmt.Printf("\n[255: finished]\n")
					Logf("go func: Received END MARK 255; exiting")
					close(channelFromPico)
					log.Fatalf("go func: Received END MARK 255; exiting")
					return

				} // end switch cmd
				/*
				 */
			} // end select
		} // end for ever
	}() // end go func

	// Infinite loop to read bytes from the serialPort
	// and copy them to the channelFromPico.
	// Panics if it cannot read the serialPort.
	serialBuffer := make([]byte, SERIAL_BUFFER_SIZE)
	for {
		n, err := serialPort.Read(serialBuffer)
		if err != nil {
			Panicf("serialPort.Read: %v", err)
		}

		for i := 0; i < n; i++ {
			//fmt.Printf("[%02x]", serialBuffer[i])
			channelFromPico <- serialBuffer[i]
		}
	}
}

func HandleWrite(regs *Regs) {
	if regs.y == 256 {
		return // It's a BLOCK operation
	}
	for i := uint(0); i < regs.y; i++ {
		ch := LPeek1(regs.x + i)
		if ch == 10 || ch == 13 {
			ShowChar('\n')
		} else {
			ShowChar(127 & ch)
		}
	}
}

func ShowChar(b byte) {
	os.Stdout.Write([]byte{b})
}

func HandleWritLn(regs *Regs) {
	switch vg.Task() {
	case 0: // Level 2, Kernel
	case 1: // Level 2, User
	case -1: // Level 1
	}
	for i := uint(0); i < regs.y; i++ {
		ch := LPeek1(regs.x + i)
		if ch == 0 {
			break
		}
		if ch == 10 || ch == 13 {
			ShowChar('\n')
		} else {
			ShowChar(127 & ch)
		}
		if 128 <= ch {
			break
		}
	}
}

type VgaGime struct {
	compat, mmu, fexx            bool
	gime_irq, gime_firq, ext_scs bool
	task                         int
	rom_mode                     int // 0,1: 16k int, 16k ext. 2: 32k int. 3: 32k ext.
}

var vg = new(VgaGime)

func (o *VgaGime) Task() int {
	if o.mmu {
		return o.task
	}
	return -1
}

func HandleIOPoke(longAddr uint, data byte) {
	a := longAddr - IO_PHYS
	switch a {
	case 0x90:
		vg.compat = (data & 0x80) != 0
		vg.mmu = (data & 0x40) != 0
		vg.gime_irq = (data & 0x20) != 0
		vg.gime_firq = (data & 0x10) != 0
		vg.fexx = (data & 0x08) != 0
		vg.ext_scs = (data & 0x04) != 0
		vg.rom_mode = int(data & 0x03)
	case 0x91:
	}
}

func GetSize(fromUSB <-chan byte) uint {
	a := getByte(fromUSB)
	if a < 128+64 {
		return uint(a & 63)
	}

	b := getByte(fromUSB)
	return 64*uint(a&63) + uint(b&63)
}

var CycleKindStr = []string{
	"?", "@", "@@", "&", "r", "w", "-", "??",
}

const (
	CY_UNUSED0 = iota
	CY_SEEN
	CY_UNSEEN
	CY_MORE
	CY_READ
	CY_WRITE
	CY_IDLE
	CY_UNUSED7
)

var LogLimit = flag.Uint64("logmax", 1<<30, "maximum bytes to log to stderr")

type LimitedLogWriter struct {
	Limit   uint64
	Current uint64
}

func InstallLimitedLogWriter() {
	llw := &LimitedLogWriter{
		Limit: *LogLimit,
	}
	log.SetOutput(llw)
}

func (llw LimitedLogWriter) Write(bb []byte) (int, error) {
	llw.Current += uint64(len(bb))
	if llw.Current > llw.Limit {
		fmt.Fprintf(os.Stderr, "\n***\nFatal: LimitedLogWriter exceeded its limit of %d bytes\n", llw.Limit)
		os.Exit(13)
	}
	return os.Stderr.Write(bb)
}
