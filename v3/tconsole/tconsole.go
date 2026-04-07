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
	"runtime"
	"runtime/debug"
	"slices"
	"strings"
	"syscall"
	"time"
)

var CURLY_DEC = flag.Bool("curly_dec", false, "Show nonprintable 7-bit output codes with curly decimal numbers")
var WIRE = flag.String("wire", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")
var DISKS = flag.String("disks", "", "Comma-separated filepaths to disk files, in order of drive number")
var USB_VERBOSE = flag.Bool("usb_verbose", false, "enable verbose debugging output of bytes over the USB")
var RAM_VERBOSE = flag.Bool("ram_verbose", false, "enable verbose debugging output of ram being written (if the pico is telling us)")
var LINKMAP = flag.String("linkmap", "", ".map file from linker")
var LINKLISTS = flag.String("linklists", "", ".list filenames from lwasm")
var ABSLISTS = flag.String("abslists", "", ".list filenames from lwasm with correct absolute addresses")
var BIND = flag.String("bind", ":8080", "WebServer binds to this address")

var CENTIPEDE = flag.Bool("centipede", false, "Centipede should set this flag")
var LEVEL = flag.Int("level", 0, "NitrOS9 level, or 0")

var the_ram Rammer
var person Personality

var LinkMap []*Section
var AbsLists []*ModSrc
var LinkLists []*ModSrc
var LinkSrc *ModSrc
var ReadCycleHistory uint64
var Os9CallsPending = make(map[string]*EventRec)

var Swi2PC uint
var Swi2Cycle uint
var RTI_PC uint
var RTICycle uint
var RTIStack uint
var RTIHistory [12]byte

const (
	C_NOP      = 0
	C_SHUTDOWN = 255

	// Long form codes, 128 to 191.
	// Followed by a 1-byte or 2-byte Size value.
	// If following byte in 128 to 191, it is 1-byte, use low 6 bits for size.
	// If following byte in 192 to 255, it is 2-byte, use low 6 bits times 64, plus low 6 bits of next byte.
	C_LOGGING = 130 // Ten levels, 130 to 139

	C_PRE_LOAD   = 163 // poke bytes packet, tconsole to tmanager: size, 2-byte addr, data[].
	C_RAM_CONFIG = 164 // Pico tells tconsole.

	C_DUMP_RAM  = 167
	C_DUMP_LINE = 168
	C_DUMP_STOP = 169
	C_DUMP_PHYS = 170

	C_EVENT      = 172
	C_DISK_READ  = 173
	C_DISK_WRITE = 174

	EVENT_RTI  = 176
	EVENT_SWI2 = 177

	// Short form codes, 192 to 255.
	// The packet length does not follow,
	// but is in the low nybble.
	C_REBOOT     = 192 // low nybble is 0.  No payload.
	C_PUTCHAR    = 193 // low nybble is 1.  Payload is "Data"
	C_RAM2_WRITE = 195 // low nybble is 3.  Payload is "AHi ALo Data"
	C_RAM3_WRITE = 196 // low nybble is 4.  Payload is "AHighest AHi ALo Data"
	C_RAM5_WRITE = 198 // low nybble is 6.  Payload is "PHighest PHi PLo AHi ALo Data"
	C_CYCLE      = 200 // one machine cycle. low nybble is 8. Payload is "cycle4 kind_fl1 data1 addr2"
	C_CYCLE_RD3  = 211 // centipede: one read cycle: A A D

	// C_NOKEY = 208  // low nybble is 0.
	// C_KEY = 211  // low nybble is 3.  Payload is { row, col, plane }
)

var CommandStrings = map[byte]string{
	C_LOGGING + 0: "C_LOGGING_0",
	C_LOGGING + 1: "C_LOGGING_1",
	C_LOGGING + 2: "C_LOGGING_2",
	C_LOGGING + 3: "C_LOGGING_3",
	C_LOGGING + 4: "C_LOGGING_4",
	C_LOGGING + 5: "C_LOGGING_5",
	C_LOGGING + 6: "C_LOGGING_6",
	C_LOGGING + 7: "C_LOGGING_7",
	C_LOGGING + 8: "C_LOGGING_8",
	C_LOGGING + 9: "C_LOGGING_9",
	C_PUTCHAR:     "C_PUTCHAR",
	C_PRE_LOAD:    "C_PRE_LOAD",
	C_RAM_CONFIG:  "C_RAM_CONFIG",
	C_DUMP_RAM:    "C_DUMP_RAM",
	C_DUMP_LINE:   "C_DUMP_LINE",
	C_DUMP_STOP:   "C_DUMP_STOP",
	C_DUMP_PHYS:   "C_DUMP_PHYS",
	C_RAM2_WRITE:  "C_RAM2_WRITE",
	C_RAM3_WRITE:  "C_RAM3_WRITE",
	C_RAM5_WRITE:  "C_RAM5_WRITE",
	C_CYCLE_RD3:   "C_CYCLE_RD3",

	C_CYCLE:       "C_CYCLE",
	C_EVENT:    "C_EVENT",
	EVENT_RTI:  "EVENT_RTI",
	EVENT_SWI2: "EVENT_SWI2",
}

var Swi2Num byte
var Swi2WriteHistory [12]byte
var Swi2WriteFuse uint
var Swi2WriteReg = [12]string{
	"CC", "A",
	"B", "DP",
	"X.hi", "X.lo",
	"Y.hi", "Y.lo",
	"U.hi", "U.lo",
	"PC.hi", "PC.lo",

	//"PC.lo", "PC.hi",
	//"U.lo", "U.hi",
	//"Y.lo", "Y.hi",
	//"X.lo", "X.hi",
	//"DP", "A",
	//"B", "CC",
}

var NormalKeys = "@ABCDEFG" + "HIJKLMNO" + "PQRSTUVW" + "XYZ^\n\b\t " + "01234567" + "89:;,-./" + "\r\014\003"
var ShiftedKeys = "@abcdefg" + "hijklmno" + "pqrstuvw" + "xyz^\n\b\t " + "\177!\"#$%&'" + "()*+<=>?" + "\r\014\003"

var LastSerialNumber uint

func MintSerial() uint {
	LastSerialNumber++
	return LastSerialNumber
}

// CpuFlags are the five extra bits on bus G1 to G5
// when the counter is in State 2.  G0 is R/W but
// we don't need to show that, because it's already
// been printed on the trace line.
var LookupCpuFlags [64]string

func init() {
	for i := 0; i < 64; i++ {
		s := ""
		if (i & 0x01) != 0 {
			// don't show the R/W bit.
		}
		if (i & 0x02) != 0 {
			s += "V" // AVMA -> V
		}
		if (i & 0x04) != 0 {
			s += "L" // LIC -> L
		}
		if (i & 0x08) != 0 {
			s += "A" // BA -> A
		}
		if (i & 0x10) != 0 {
			s += "S" // BS -> S
		}
		if (i & 0x20) != 0 {
			s += "Y" // BUSY -> Y
		}
		LookupCpuFlags[i] = s
	}
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
	x := <-fromUSB
	logGetByte(x, "   ")
	return x
}
func logGetByte(x byte, why string) {
	if *USB_VERBOSE {
		out := ""
		if why == "cmd" {
			out = ">>>>>"
		}
		if 32 <= x && x <= 126 {
			log.Printf("GetByte %s .... %02x = '%c' %s", why, x, x, out)
		} else if x == 13 {
			log.Printf("GetByte %s ---- %02x ------------------- %s", why, x, out)
		} else if x == 10 {
			log.Printf("GetByte %s ==== %02x =================== %s", why, x, out)
		} else if x == 0 {
			log.Printf("GetByte %s .... %02x 0000000000000000000 %s", why, x, out)
		} else {
			s, _ := CommandStrings[x]
			log.Printf("GetByte %s ---- %02x     (%d. %q)", why, x, x, s)
		}
	}
}

func WriteBytes(channelToPico chan []byte, vec ...byte) {
	Logf("WriteBytes: [%d.] { % 3x }", len(vec), vec)
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

func TryRun(inkey chan byte, person Personality) {
	defer func() {
		r := recover()
		if r != nil {
			fmt.Printf("[recover: %q]\n", r)
		}
	}()
	Run(inkey, person)
}

func SttyCbreakMode(turnOn bool) {
	// See also https://github.com/SimonWaldherr/golang-minigames/blob/master/snake.go
	sttyPath, err := exec.LookPath("stty")
	if err != nil {
		log.Panicf("Cannot find stty: %v", err)
	}
	// Turn off values:
	toCbreak := "-cbreak"
	toEcho := "echo"
	if turnOn {
		// Turn on values:
		toCbreak = "cbreak"
		toEcho = "-echo"
	}
	cmd := &exec.Cmd{
		Path:   sttyPath,
		Args:   []string{"stty", toCbreak, toEcho},
		Stdin:  os.Stdin,
		Stdout: os.Stdout,
		Stderr: os.Stderr,
	}
	err = cmd.Run()
	if err != nil {
		log.Panicf("Cannot run stty: %v", err)
	}
}

func main() {
	log.SetFlags(0)
	flag.Parse()
	InstallLimitedLogWriter()
	// println("Font8x8 font len", len(Font8x8))

	if runtime.GOOS != "windows" {
		SttyCbreakMode(true)
	}
	defer func() { Shutdown(recover()) }()

	switch *LEVEL {
	case 0:
		the_ram = new(Coco1Ram)
		person = new(Plain)

	case 1:
		the_ram = new(Coco1Ram)
		person = new(Os9Level1)

	case 2:
		the_ram = new(Coco3Ram)
		person = new(Os9Level2)

	default:
		log.Panicf("Bad NitrOS9 Level: %d", *LEVEL)
	}

	inkey := make(chan byte, 1024)
	go InkeyRoutine(inkey)

	killed := make(chan os.Signal, 16)
	signal.Notify(killed, syscall.SIGINT)
	go func() {
		defer func() { Shutdown(recover()) }()
		sig := <-killed
		Panicf("STOPPING ON SIGNAL %q", sig)
	}()

	if *ABSLISTS != "" {
		for _, filename := range strings.Split(*ABSLISTS, ",") {
			if filename == "" {
				continue
			}
			lf := LoadFile(filename)
			AbsLists = append(AbsLists, lf)
			log.Printf("LOADED ABS LIST_FILENAME %q (%d)", filename, len(lf.Src))

			if false {
				for k, v := range lf.Src {
					log.Printf("ITEM_LOADED ABS %04x :: %q :: %q", k, v, filename)
				}
			}
		}
	}

	if true || *LINKMAP != "" {
		LinkMap = ReadMap(*LINKMAP)

		for _, filename := range strings.Split(*LINKLISTS, ",") {
			if filename == "" {
				continue
			}
			lf := LoadFile(filename)
			LinkLists = append(LinkLists, lf)
			log.Printf("LOADED LIST_FILENAME %q (%d)", filename, len(lf.Src))

			for k, v := range lf.Src {
				log.Printf("ITEM_LOADED %04x :: %q :: %q", k, v, filename)
			}
		}
		LinkSrc = ComputeLinkSrc(LinkMap, LinkLists, AbsLists)
		log.Printf("ComputeLinkSrc returns %d items", len(LinkSrc.Src))

		if false {
			var keys []uint
			for k := range LinkSrc.Src {
				keys = append(keys, k)
			}
			slices.Sort(keys)

			for _, k := range keys {
				v := LinkSrc.Src[k]
				log.Printf("ComputeLinkSrc %04x -> %s", k, v)
			}
		}
	}

	if *BIND != "" {
		go WebServer(&WebConsoleConfig{
			Bind: *BIND,
			Key: func(flags uint, s string) {
				ch := KeystrokeValue(flags, s)
				if 1 <= ch && ch <= 127 {
					inkey <- ch
				}
			},
			Move: func(x, y int) {},
			Down: func(x, y int) {},
			Up:   func(x, y int) {},
		})
		time.Sleep(100 * time.Millisecond)
	}
	OpenDisks(*DISKS)
	for {
		TryRun(inkey, person)
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
		sz, err := os.Stdin.Read(bb)
		if err != nil {
			Panicf("cannot os.Stdin.Read: %v", err)
		}
		if bb[0] == 127 {
			// Change DEL to BS for OS9
			bb[0] = 8
			Logf("Inkey: Changing DEL to BS")
		}
		Logf("Inkey: $%02x = %d. = %q", bb[0], bb[0], bb)
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

func Shutdown(r any) {
	if r != nil {
		fmt.Printf("***\n*** CAUGHT ERROR: %v\n***\n", r)
		fmt.Fprintf(os.Stderr, "***\n*** CAUGHT ERROR: %v\n***\n", r)
	}

	SttyCbreakMode(false)

	if the_ram != nil {
		the_ram.Dump()
	}

	fmt.Printf("*** SHUTDOWN\n")
	fmt.Fprintf(os.Stderr, "*** SHUTDOWN\n")
	debug.PrintStack()
	os.Exit(13)
}

func ToUsbRoutine(w io.Writer, channelToPico chan []byte) {
	defer func() { Shutdown(recover()) }()

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

var Cycle uint

func RunSelect(inkey chan byte, fromUSB <-chan byte, channelToPico chan []byte, channelFromPico chan byte, person Personality) {
	defer func() { Shutdown(recover()) }()

	loadArgs := flag.Args()
	if *CENTIPEDE {
		loadArgs = nil // Nothing to load (yet) in centipede0 mode.
	}

	var previousPutChar byte
	var remember int64
	var timer_sum int64
	var timer_count int64
	pending := make(map[string]*EventRec)

	// gap := 1 // was for C_KEY, C_NOKEY
	for {
		select {
		case inchar := <-inkey: // SELECT CASE user typed a character
			switch inchar {
			case 31: // Control Underscore (^_)
				fmt.Printf("\n*** REBOOT PICO ***\n")
				WriteBytes(channelToPico, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT, C_REBOOT)
			}
			if 1 <= inchar && inchar <= 127 {
				WriteBytes(channelToPico, inchar)
			}

		case cmd := <-fromUSB: // SELECT CASE Pico sent a byte over the USB.
			logGetByte(cmd, "cmd")

			bogus := 0

			var ch byte // Used by default and C_PUTCHAR

			switch cmd {

			case C_NOP:
				// NO OP.
				Logf("C_NOP")

			case C_RAM_CONFIG:
				pack := GetPacket(fromUSB, cmd)
				if len(pack) >= 1 {
					log.Printf("C_RAM_CONFIG: $%x", pack[0])
					switch pack[0] {
					case '1':
						the_ram = new(Coco1Ram)
						person = new(Os9Level1)

					case '2':
						the_ram = new(Coco3Ram)
						person = new(Os9Level2)

					default:
						log.Panicf("C_RAM_CONFIG size %d unknown value: % 3x", len(pack), pack)
					}
					if len(pack) >= 2 {
						switch pack[0] {
						case '3':
							// TODO -- straighten this out
							person = new(Plain)
						case '8':
							// TODO -- straighten this out
							person = new(Plain)
						}
					}
				} else {
					log.Panicf("C_RAM_CONFIG unknown size %d: % 3x", len(pack), pack)
				}

			case C_CYCLE:
				const GLOSS = true
				pack := GetPacket(fromUSB, cmd)
				if len(pack) == 8 {
					_cy := (uint(pack[0]) << 24) + (uint(pack[1]) << 16) + (uint(pack[2]) << 8) + uint(pack[3])
					_fl := pack[4] & 31
					_kind := pack[4] >> 5
					_data := pack[5]
					_addr := (uint(pack[6]) << 8) + uint(pack[7])

					var s string
					if _kind == CY_IDLE {
						Logf("cy - ---- -- %s#%d", LookupCpuFlags[_fl], _cy)
					} else {
						s = Format("cy %s %04x %02x %s#%d", CycleKindStr[_kind], _addr, _data, LookupCpuFlags[_fl], _cy)

						if person.HasMMap() {
							phys := the_ram.Physical(uint(_addr))
							if _kind == CY_SEEN_OP || _kind == CY_UNSEEN_OP {
								if GLOSS {
									GlossFirstCycle(_addr, _data) // has side-effect to start the glossing for the cycle
								}
								// The first cycle of an instruction
								modName, modOffset := person.MemoryModuleOf(phys)
								mmap := person.CurrentHardwareMMap()
								Logf("%s %s%%%06x :%q+%04x %s", s, mmap, phys, modName, modOffset, AsmSourceLine(modName, modOffset))
							} else {
								g := ""
								if GLOSS {
									g = GlossLaterCycle(_addr, _data)
								}
								// later cycles in an instruction
								Logf("%s %%%06x%s", s, phys, g)
							}
						} else {
							if _kind == CY_SEEN_OP || _kind == CY_UNSEEN_OP {
								if GLOSS {
									GlossFirstCycle(_addr, _data) // has side-effect to start the glossing for the cycle
								}
								// The first cycle of an instruction
								modName, modOffset := person.MemoryModuleOf(_addr)
								Logf("%s :%q+%04x %s", s, modName, modOffset, AsmSourceLine(modName, modOffset))
							} else {
								g := ""
								if GLOSS {
									g = GlossLaterCycle(_addr, _data)
								}
								// later cycles in an instruction
								Logf("%s %s", s, g)
							}
						}
					}
				}

			case C_CYCLE_RD3: // centipede: A A D
				const GLOSS = true
				pack := GetPacket(fromUSB, cmd)
				if len(pack) == 3 {
					_data := pack[2]
					_addr := (uint(pack[0]) << 8) + uint(pack[1])

					if *CENTIPEDE {
                        Cycle++

						modName, modOffset := person.MemoryModuleOf(_addr)
						aline := Format("%q+%04x %s", modName, modOffset, AsmSourceLine(modName, modOffset))
						// aline, _ := LinkSrc.Src[_addr]
						cline := Format("cy-r %04x   -> %02x  #%d  %s", _addr, _data, Cycle, aline)
						Logf("%s", cline)

						ReadCycleHistory = (ReadCycleHistory << 8) | uint64(_data)

						switch {
						case ReadCycleHistory == 0x20FE20FE20FE20FE:
							{
								Logf("INFINITE LOOP")

								log.Panic("INFINITE LOOP")
							}
						case (ReadCycleHistory & 0xFFFF00) == 0x103F00:
							{
								Logf("GOT SWI2(%02x)", _data)
								Swi2WriteFuse = 12
                                Swi2PC = _addr - 2
                                Swi2Cycle = Cycle
								Swi2Num = _data
							}
                        case _data == 0x3B: // RTI
                            {
                                RTI_PC = _addr
                                RTICycle = Cycle
                            }
                        case (ReadCycleHistory & 0xFF00) == 0x3B00:
                            {
                                // intermediate step
                            }
                        case (ReadCycleHistory & 0xFF0000) == 0x3B0000:
                            {
                                RTIStack = _addr
                                RTIHistory[0] = _data
                            }
                        case RTIStack != 0:
                            {
                                i := _addr - RTIStack
                                // Logf("R::: (%x) i=%d. addr %x S %x | % 3x", RTICycle, i, _addr, RTIStack, RTIHistory)
                                if i >= 12 {
                                    key := Format("%04x_%04x", _addr-12, ((uint(RTIHistory[10]) << 8) | uint(RTIHistory[11]) - 3 ))
                                    rec, _ := Os9CallsPending[key]
                                    snum := 0
                                    call := "?"
                                    if rec != nil {
                                        snum = int(rec.SerialNum)
                                        call = rec.Call
                                        delete(Os9CallsPending, key)
                                    }
                                    status := "OKAY"
                                    if (RTIHistory[0] & 1) != 0 {
                                        status = Format("ERROR($%x=%d.)", RTIHistory[2], RTIHistory[2])
                                    }
                                    Logf("RTI: %s (%x) PC %x S %x :: %s :: % 3x <== _%d_ %v", key, RTICycle, RTI_PC, RTIStack, status, RTIHistory, snum, call)
                                    RTI_PC = 0
                                    RTICycle = 0
                                    RTIStack = 0
                                } else {
                                    RTIHistory[i] = _data
                                }
                            }
                        default:
                            {
                                RTI_PC = 0
                                RTICycle = 0
                                RTIStack = 0
                            }
						}
					}
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
				pack := GetPacket(fromUSB, cmd)
				Logf("LOG[%d]: %q", cmd-C_LOGGING, pack)

			case C_DISK_WRITE:
				//Logf("C_DISK_WRITE[%d]: ...", 111)
				pack := GetPacket(fromUSB, cmd)
				//Logf("C_DISK_WRITE[%d]: %q ...", 222, pack)
				EmulateDiskWrite(pack, channelToPico)
				//Logf("C_DISK_WRITE[%d]: %q", 333, pack)

			case C_DISK_READ:
				pack := GetPacket(fromUSB, cmd)
				EmulateDiskRead(pack, channelToPico)

			case C_EVENT:
				pack := GetPacket(fromUSB, cmd)
				OnEvent(pack, pending, person)

			case C_RAM3_WRITE:
				panic("C_RAM3_WRITE not imp")

			case C_RAM5_WRITE:
				pack := GetPacket(fromUSB, cmd)
				AssertEQ(len(pack), 6)

				ptop := pack[0]
				phi := pack[1]
				plo := pack[2]
				phys := (uint(ptop) << 16) | (uint(phi) << 8) | uint(plo)

				hi := pack[3]
				lo := pack[4]
				addr := (uint(hi) << 8) | uint(lo)

				data := pack[5]

				if *RAM_VERBOSE {
					mapped := the_ram.Physical(addr)
					Logf("  =PRAM= %04x <m %06x =d %06x >p %06x gets %02x (was %02x)", addr, mapped, phys-mapped, phys, data, the_ram.Peek1(addr))
					if mapped != phys {
						log.Printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ W5")
						debug.PrintStack()
						log.Printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ W5")
						AssertEQ(mapped, phys)
					}
				}

				the_ram.Poke1(addr, data)

				if (addr & 0xFF00) == 0xFF00 {
					HandleIOPoke(addr, data)
				}

			case C_RAM2_WRITE:
				pack := GetPacket(fromUSB, cmd)
				AssertEQ(len(pack), 3)

				hi := pack[0]
				lo := pack[1]
				addr := (uint(hi) << 8) | uint(lo)

				data := pack[2]
				//fmt.Printf("W %04x %02x\n", addr, data)
				// fmt.Printf("^");

				if *CENTIPEDE {
                    Cycle++

					_data := pack[2]
					_addr := (uint(pack[0]) << 8) + uint(pack[1])
					the_ram.Poke1(_addr, _data)
					gloss := "   "
					switch _data >> 5 {
					case 0:
						gloss = Format("<%c>", 64+(31&data))
					case 1:
						gloss = Format("<%c>", 32+(31&data))
					case 2:
						gloss = Format(" %c ", 64+(31&data))
					case 3:
						gloss = Format(" %c ", 32+(31&data))
					}
					explain := false
					if Swi2WriteFuse > 0 {
						Swi2WriteFuse--
						Swi2WriteHistory[Swi2WriteFuse] = _data
						gloss += "        =" + Swi2WriteReg[Swi2WriteFuse]

						if Swi2WriteFuse == 0 {
							explain = true
						}
					}
					cline := Format("cy-w %04x <-  %02x  #%d  %s", _addr, _data, Cycle, gloss)
					Logf("%s", cline)
					if explain {
						ExplainOs9Call(_addr, _data, Swi2Num)
					}
				} else {
					if *RAM_VERBOSE {
						Logf("  =RAM= %04x %%%06x gets %02x (was %02x)", addr, the_ram.Physical(addr), data, the_ram.Peek1(addr))
					}
					the_ram.Poke1(addr, data)

					if (addr & 0xFF00) == 0xFF00 {
						HandleIOPoke(addr, data)
					}
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

						/*
							if cmd == C_DUMP_PHYS {
								for j := uint(0); j < 16; j++ {
									longaddr := (uint(a)<<16 | uint(b)<<8 | uint(c)) + j
									longaddr %= the_ram.RamSize()
									if d[j] != the_ram.GetTrackRam()[longaddr] {
										Logf("--- WRONG PHYS %06x ( %02x vs %02x ) ---", longaddr, d[j], the_ram.GetTrackRam()[longaddr])
									}
								}
							}
						*/

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
						Logf("FUNNY CHAR DURING DUMP: %d.", what)
						bogus++
						if bogus > 10 {
							bogus = 0
							break DUMPING
						}
					}
				}
				Logf("}}} %s", CommandStrings[cmd])

			default:
				if 1 <= cmd && cmd <= 127 {
					ch = cmd
				} else {
					log.Printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ X")
					debug.PrintStack()
					log.Printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Y")
					log.Panicf("cmd == %d", cmd)
					panic(cmd)
				}
				fallthrough

			case C_PUTCHAR:
				if cmd == C_PUTCHAR {
					ch = getByte(fromUSB)
				} // otherwise use the ch from default case.

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
					if loadArgs != nil && LookForPreSync(ch) {
						PreUploadArgs(loadArgs, channelToPico)
						loadArgs = nil // now LOAD is empty, so we don't load again.
					}

				case ch == 7 || ch == 8: // BEL, BS
					fmt.Printf("%c", ch)

				case ch == 10 || ch == 13:
					fmt.Printf("%c", ch)
					/*
						if previousPutChar == 10 || previousPutChar == 13 {
							// skip extra newline
						} else {
							fmt.Println() // lf skips Println after cr does Println
						}
					*/

				default:
					if *CURLY_DEC {
						fmt.Printf("{%d}", ch) // Use curly decimal to make it printable.
					} else {
						fmt.Printf("%c", ch) // control sequences allowed.
					}
					cr = false
				} // end inner switch on ch range
				previousPutChar = ch

				/*
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
				*/
			case C_SHUTDOWN:
				fmt.Printf("\n[255: shutdown]\n")
				Logf("go func: Received C_SHUTDOWN; exiting")
				close(channelFromPico)
				log.Panicf("go func: C_SHUTDOWN")
				return

			} // end switch cmd
			/*
			 */
		} // end select
	} // end for ever
} // RunSelect

func Run(inkey chan byte, person Personality) {
	const SERIAL_BUFFER_SIZE = 1024

	// Set up options for Serial Port.
	serialOptions := OpenSerialOptions{
		PortName:        *WIRE,
		BaudRate:        *BAUD,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}
	// Open the Serial Port.
	serialPort, err := OpenSerial(serialOptions)
	if err != nil {
		Panicf("serial.Open: %v", err)
	}

	// Make sure to close it later.
	defer serialPort.Close()

	channelToPico := make(chan []byte, 1024)

	go ToUsbRoutine(serialPort, channelToPico)

	channelFromPico := make(chan byte, SERIAL_BUFFER_SIZE)
	var fromUSB <-chan byte = channelFromPico

	go RunSelect(inkey, fromUSB, channelToPico, channelFromPico, person)

	go TextDaemon()

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
	/*
		if regs.y == 256 {
			return // It's a BLOCK operation
		}

		for i := uint(0); i < regs.y; i++ {
			ch := the_ram.LPeek1(regs.x + i)
			if ch == 10 || ch == 13 {
				ShowChar('\n')
			} else {
				ShowChar(127 & ch)
			}
		}
	*/
}

func ShowChar(b byte) {
	os.Stdout.Write([]byte{b})
}

func HandleWritLn(regs *Regs) {
	/*
	   switch vg.Task() {
	   case 0: // Level 2, Kernel
	   case 1: // Level 2, User
	   case -1: // Level 1
	   }

	   	for i := uint(0); i < regs.y; i++ {
	   		ch := the_ram.LPeek1(regs.x + i)
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
	*/
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
	a := longAddr - the_ram.IoPhys()
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

func GetPacket(fromUSB <-chan byte, cmd byte) []byte {
	AssertGE(cmd, 0x80)
	sz := uint(cmd) & 0x0F // low nybble
	if 0x80 <= cmd && cmd <= 0xC0 {
		sz = GetSize(fromUSB)
	}
	pack := make([]byte, sz)
	for i := uint(0); i < sz; i++ {
		pack[i] = <-fromUSB
	}
	if *USB_VERBOSE {
		if sz > 64 {
			Logf("GetPacket (sz=%d.)  % 3x ...", sz, pack[:64])
		} else {
			Logf("GetPacket (sz=%d.)  % 3x", sz, pack)
		}
	}
	return pack
}
func GetSize(fromUSB <-chan byte) uint {
	a := <-(fromUSB)
	if a < 128+64 {
		z := uint(a & 63)
		if *USB_VERBOSE {
			Logf("GetSize.............. [%x] => $%x = %d.", a, z, z)
		}
		return z
	}

	b := <-(fromUSB)
	z := 64*uint(a&63) + uint(b&63)
	if *USB_VERBOSE {
		Logf("GetSize.............. [%x %x] => $%x = %d.", a, b, z, z)
	}
	return z
}
func PutSize(channelToPico chan []byte, sz uint) {
	AssertLT(sz, 4096)
	if sz < 64 {
		WriteBytes(channelToPico, byte(128+sz))
	} else {
		WriteBytes(channelToPico, byte(192+(sz>>6)), byte(128+(sz&63))) // div 64, mod 64
	}
}

var CycleKindStr = []string{
	"?", "@", "@@", "&", "r", "w", "-", "??",
}

const (
	CY_UNUSED0 = iota
	CY_SEEN_OP
	CY_UNSEEN_OP
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
	if *LogLimit > 0 {
		llw := &LimitedLogWriter{
			Limit: *LogLimit,
		}
		log.SetOutput(llw)
	}
}

func (llw *LimitedLogWriter) Write(bb []byte) (int, error) {
	llw.Current += uint64(len(bb))
	if llw.Current > llw.Limit {
		fmt.Fprintf(os.Stderr, "\n*** FATAL: LimitedLogWriter exceeded its limit of %d bytes (use --logmax=B to change the limit to B bytes)\n", llw.Limit)
		os.Exit(13)
	}
	return os.Stderr.Write(bb)
}

var syncWindow [4]byte

func LookForPreSync(ch byte) bool {
	copy(syncWindow[0:3], syncWindow[1:4])
	syncWindow[3] = ch
	Logf("LookForPreSync: %q vs %q", syncWindow[:], ".:,;")
	return string(syncWindow[:]) == ".:,;"
}

func ExplainOs9Call(_addr uint, _data byte, os9num byte) {
    Logf("\nExplainOs9Call: a=%x d=%x num=%x", _addr, _data, os9num)
	rec := &EventRec{
		SerialNum: MintSerial(),
		Os9Num:    Swi2Num,
		Datas:     make([]byte, 14),
	}

	for i, h := range Swi2WriteHistory {
		rec.Datas[11-i+2] = h
	}

	call, _ := Os9ApiCallOf[os9num]
	callString, regs := person.FormatCall(os9num, call, rec)
	rec.Call = callString

    key := Format("%04x_%04x", _addr, Swi2PC)
    Logf("\n%s === OS9_CALL _%d_ %s %#v", key, rec.SerialNum, callString, regs)
	Logf("\n%s === EventRec %#v", key, rec)
	Logf("\n")

    Os9CallsPending[key] = rec

    registered := person.RegisteredMemoryModules()
    if registered == nil {
        if RecentScannedMemoryModules != nil {
            for i, m := range RecentScannedMemoryModules {
                Logf("Scanned [% 2x] %04x-%04x  %04x %q   %q", i, m.Addy, m.Addy+m.Size, m.Size, m.Name, m.FullName)
            }
        }
    } else {
        for i, m := range person.RegisteredMemoryModules() {
            Logf("Registered [% 2x] %04x-%04x  %04x %q   %q", i, m.Addy, m.Addy+m.Size, m.Size, m.Name, m.FullName)
        }
	}
}
