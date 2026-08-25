package main

import (
	"github.com/strickyak/tfr9-merging-centipede/v1/lib"
	"github.com/strickyak/tfr9-merging-centipede/v1/tether/cobs"

	"bytes"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"runtime/debug"
	"slices"
	"strings"
	"syscall"
	"time"
	"sync/atomic"
)

var OMIT_STDERR = flag.Bool("omit_stderr", false, "send stderr to nowhere")
var NO_KEYBOARD = flag.Bool("n", false, "disable keyboard input")
var CURLY_DEC = flag.Bool("curly_dec", false, "Show nonprintable 7-bit output codes with curly decimal numbers")
var WIRE = flag.String("wire", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")
var DISKS = flag.String("disks", "", "Comma-separated filepaths to disk files, in order of drive number")
var PC_DIR = flag.String("pc", "/tmp/pc", "root directory for virtual /pc filesystem")
var USB_VERBOSE = flag.Bool("usb_verbose", false, "enable verbose debugging output of bytes over the USB")
var RAM_VERBOSE = flag.Bool("ram_verbose", false, "enable verbose debugging output of ram being written (if the pico is telling us)")
var LINKMAP = flag.String("linkmap", "", ".map file from linker")
var LINKLISTS = flag.String("linklists", "", ".list filenames from lwasm")
var ABSLISTS = flag.String("abslists", "", ".list filenames from lwasm with correct absolute addresses")
var BIND = flag.String("bind", ":8080", "WebServer binds to this address")
var TETHER_LOG_USB = flag.Bool("tether_log_usb", false, "Log USB COBS and RPC traffic")
var QUICK_PING = flag.Int("quick-ping", -1, "Quick mode: connect, send a PicoRPC ping with this uint32 payload, print result, and exit")
var QUICK_RESTART = flag.Uint("quick-restart", 0, "Quick mode: connect and restart the Pico firmware with this boot_mode")
var BOOTMODE = flag.Uint("bootmode", 0, "Restart firmware in this mode and then run tether console as usual")
var QUICK_REFLASH = flag.String("quick-reflash", "", "Quick mode: reboot Pico into BOOTSEL and copy this UF2 file to it")
var QUICK_REFORMAT = flag.Bool("quick-reformat", false, "Quick mode: connect and reformat the Pico's LittleFS flash filesystem")
var QUICK_INJECT = flag.String("quick-inject", "", "Quick mode: connect and inject a Tcl command to the Pico's REPL")
var QUICK_UPLOAD = flag.String("quick-upload", "", "Quick mode: upload the Pico's flash from this zip file")
var QUICK_LABEL_DATA = flag.String("quick-label-data", "", "Quick mode: create a uf2 file with this data for the label. Must begin with `p=1,`")
var QUICK_LABEL_FILENAME = flag.String("quick-label-filename", "_metadata.uf2", "Where to write the uf2 file for assigning a label")

var tmpDirToClean string

var tetherLogUsbSerial uint64

var CENTIPEDE = flag.Bool("centipede", true, "Centipede should set this flag")
var LEVEL = flag.Int("level", 0, "NitrOS9 level, or 0")
var COBS_CHECKSUMS = flag.Bool("cobs_checksums", true, "Enable COBS packet checksums")

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
	C_LOGGING = 130 // Ten levels, 130 to 139

	C_PRE_LOAD   = 163 // poke bytes packet, tconsole to tmanager: size, 2-byte addr, data[].
	C_RAM_CONFIG = 164 // Pico tells tconsole.

	C_DUMP_RAM  = 167
	C_DUMP_LINE = 168
	C_DUMP_STOP = 169
	C_DUMP_PHYS = 170

	C_EVENT             = 172
	C_DISK_READ         = 173
	C_DISK_WRITE        = 174
	C_COMPRESSED_CYCLES = 175

	EVENT_RTI  = 176
	EVENT_SWI2 = 177

	// Short form codes, 192 to 255.
	// The packet length does not follow,
	// but is in the low nybble.
	C_REBOOT      = 192 // low nybble is 0.  No payload.
	C_PUTCHAR     = 193 // low nybble is 1.  Payload is "Data"
	C_WRITE_CYCLE = 195 // *** low nybble is 3.  Payload is "AHi ALo Data"
	C_RAM3_WRITE  = 196 // low nybble is 4.  Payload is "AHighest AHi ALo Data"
	C_RAM5_WRITE  = 198 // low nybble is 6.  Payload is "PHighest PHi PLo AHi ALo Data"
	C_CYCLE       = 200 // one machine cycle. low nybble is 8. Payload is "cycle4 kind_fl1 data1 addr2"
	C_READ_CYCLE  = 211 // *** centipede: one read cycle: A A D
	C_FIC_CYCLE   = 227 // 0xE3: First Instruction Cycle: A A D (with source lookup)

	// C_NOKEY = 208  // low nybble is 0.
	// C_KEY = 211  // low nybble is 3.  Payload is { row, col, plane }

	// T_*: From Tether to Pico:
	T_DISK_READ = 173
	T_HELLO     = 178
	T_COMMAND   = 179
	T_RPC       = 180
	T_PICO_RPC  = 181
)

var cmdChan = make(chan string, 10)

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
	C_WRITE_CYCLE: "C_WRITE_CYCLE",
	C_RAM3_WRITE:  "C_RAM3_WRITE",
	C_RAM5_WRITE:  "C_RAM5_WRITE",
	C_READ_CYCLE:  "C_READ_CYCLE",
	C_FIC_CYCLE:   "C_FIC_CYCLE",

	C_CYCLE:    "C_CYCLE",
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
	if *TETHER_LOG_USB {
		ser := atomic.AddUint64(&tetherLogUsbSerial, 1)
		log.Printf("_%d COBS OUT: len=%d %x", ser, len(vec), vec)
	}
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

func main() {
	log.SetFlags(0)
	flag.Parse()
	cobs.UseChecksums = *COBS_CHECKSUMS
	InstallLimitedLogWriter()

	if *QUICK_UPLOAD != "" {
		tmp, err := os.MkdirTemp("", "quick-upload-*")
		if err != nil {
			log.Fatalf("FAILED TO CREATE TEMP DIR: %v", err)
		}
		tmpDirToClean = tmp
		if strings.Contains(*QUICK_UPLOAD, "=") {
			var injectCmds []string
			pairs := strings.Split(*QUICK_UPLOAD, ",")
			for i, pair := range pairs {
				parts := strings.SplitN(pair, "=", 2)
				if len(parts) != 2 {
					log.Fatalf("INVALID QUICK_UPLOAD SYNTAX (expected target=source): %q", pair)
				}
				target := parts[0]
				source := parts[1]
				
				in, err := os.Open(source)
				if err != nil {
					log.Fatalf("FAILED TO OPEN SOURCE FILE %q: %v", source, err)
				}
				
				tmpName := fmt.Sprintf("tmp%d", i)
				outPath := filepath.Join(tmp, tmpName)
				out, err := os.Create(outPath)
				if err != nil {
					log.Fatalf("FAILED TO CREATE TEMP FILE %q: %v", outPath, err)
				}
				
				_, err = io.Copy(out, in)
				if err != nil {
					log.Fatalf("FAILED TO COPY FILE %q: %v", source, err)
				}
				in.Close()
				out.Close()
				
				injectCmds = append(injectCmds, fmt.Sprintf("cp /pc/%s %s", tmpName, target))
			}
			*PC_DIR = tmp
			*QUICK_INJECT = strings.Join(injectCmds, " ; ")
		} else {
			in, err := os.Open(*QUICK_UPLOAD)
			if err != nil {
				log.Fatalf("FAILED TO OPEN UPDATE ZIP: %v", err)
			}
			outPath := filepath.Join(tmp, "update.zip")
			out, err := os.Create(outPath)
			if err != nil {
				log.Fatalf("FAILED TO CREATE TEMP ZIP: %v", err)
			}
			_, err = io.Copy(out, in)
			if err != nil {
				log.Fatalf("FAILED TO COPY ZIP: %v", err)
			}
			in.Close()
			out.Close()

			*PC_DIR = tmp
			cmd := "rsync-a /pc/update.zip! /"
			*QUICK_INJECT = cmd
		}
	}

	// Quick-ping mode: connect, ping, exit.
	if *QUICK_PING >= 0 {
		RunQuickPing(uint32(*QUICK_PING))
		return
	}
	// Quick-restart mode: connect and restart the Pico.
	var doQuickRestart bool
	var doBootMode bool
	flag.Visit(func(f *flag.Flag) {
		if f.Name == "quick-restart" {
			doQuickRestart = true
		}
		if f.Name == "bootmode" {
			doBootMode = true
		}
	})
	if doQuickRestart {
		RunQuickRestart(uint32(*QUICK_RESTART), true)
		return
	}
	if doBootMode {
		RunQuickRestart(uint32(*BOOTMODE), false)
		// Do not return, continue to normal console
	}
    if *QUICK_LABEL_DATA != "" {
        if !strings.HasPrefix(*QUICK_LABEL_DATA, "p=1,") {
            log.Fatalf("Label must begin with %q but is %q", "p=1,", *QUICK_LABEL_DATA)
        }
        MakeUf2Label(*QUICK_LABEL_DATA, *QUICK_LABEL_FILENAME);
        return
    }
	// Quick-reflash mode: connect, reboot into BOOTSEL, copy UF2 file.
	if *QUICK_REFLASH != "" {
		RunQuickReflash(*QUICK_REFLASH)
		return
	}
	// Quick-reformat mode: connect and reformat LittleFS.
	if *QUICK_REFORMAT {
		RunQuickAction("reformat")
		return
	}

    // attempt to make /tmp/tether or whatever the --fs directory is
	os.Mkdir(*PC_DIR, 0777)

	if runtime.GOOS != "windows" && *QUICK_INJECT == "" {
		SaveSttyState()
		SetSttyCbreak()
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
		Panicf("Bad NitrOS9 Level: %d", *LEVEL)
		panic(0)
	}

	inkey := make(chan byte, 1024)
	if !*NO_KEYBOARD {
		go InkeyRoutine(inkey)
	}

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
				if ch >= 1 {
					if !*NO_KEYBOARD {
						inkey <- ch
					}
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

	var lineBuf []byte
	atStartOfLine := true
	readingCmd := false

	// ANSI escape sequence parser state
	escBuf := []byte{}
	escActive := false
	escTimer := time.NewTimer(0)
	if !escTimer.Stop() {
		<-escTimer.C
	}

	sendKey := func(ch byte) {
		if ch == 10 {
			// Linux terminal sends LF for Enter; firmware expects CR
			ch = 13
		}
		if ch == 127 {
			// Change DEL to BS
			ch = 8
			Logf("Inkey: Changing DEL to BS")
		}
		Logf("Inkey: $%02x = %d.", ch, ch)
		inkey <- ch
	}

	// Single persistent goroutine to read stdin bytes
	rawCh := make(chan byte, 16)
	go func() {
		for {
			bb := make([]byte, 1)
			_, err := os.Stdin.Read(bb)
			if err != nil {
				Panicf("cannot os.Stdin.Read: %v", err)
			}
			rawCh <- bb[0]
		}
	}()

	for {
		// Wait for a byte, with ESC timeout if in escape sequence
		var ch byte
		if escActive {
			select {
			case ch = <-rawCh:
				// Got a byte while in escape sequence
			case <-escTimer.C:
				// ESC timed out — send bare ESC and reset
				escActive = false
				sendKey(27)
				escBuf = nil
				continue
			}
		} else {
			ch = <-rawCh
		}

		// Process escape sequences
		if escActive {
			escBuf = append(escBuf, ch)
			s := string(escBuf)

			// Check if this is a complete sequence
			done := false
			var mapped byte

			if len(escBuf) == 1 {
				if ch == '[' || ch == 'O' {
					// Continue collecting
				} else {
					// Unknown after ESC — send ESC + this char
					sendKey(27)
					escActive = false
					escBuf = nil
					// Fall through to process ch normally below
					goto normalKey
				}
			} else {
				// Multi-byte sequence: check for terminal letter or ~
				if (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '~' {
					done = true
					switch s {
					case "[A", "OA":
						mapped = 128 // Up
					case "[B", "OB":
						mapped = 129 // Down
					case "[C", "OC":
						mapped = 131 // Cursor Right (non-destructive)
					case "[D", "OD":
						mapped = 130 // Cursor Left (non-destructive)
					case "[1;2A": // Shift-Up
						mapped = 132 // Page Up
					case "[1;2B": // Shift-Down
						mapped = 133 // Page Down
					case "[1;2C": // Shift-Right
						mapped = 131 // Cursor Right (same as plain)
					case "[1;2D": // Shift-Left
						mapped = 130 // Cursor Left (same as plain)
					case "[5~": // Page Up
						mapped = 132
					case "[6~": // Page Down
						mapped = 133
					default:
						mapped = 0 // Unknown sequence, discard
					}
				} else if len(escBuf) > 8 {
					// Too long, discard
					done = true
					mapped = 0
				}
				// Otherwise keep collecting (e.g. "[1;2" not yet complete)
			}

			if done {
				escActive = false
				escBuf = nil
				if mapped != 0 {
					sendKey(mapped)
				}
			}
			continue
		}

		if ch == 27 && !readingCmd {
			// Start escape sequence collection
			escActive = true
			escBuf = nil
			escTimer.Reset(100 * time.Millisecond)
			continue
		}

	normalKey:
		if readingCmd {
			if len(lineBuf) == 0 && ch == '~' {
				readingCmd = false
				atStartOfLine = false
				os.Stdout.Write([]byte{8, ' ', 8})
				// Fall through to send the ~ over USB
			} else {
				if ch == '\r' || ch == '\n' {
					cmdChan <- string(lineBuf)
					readingCmd = false
					atStartOfLine = true
					os.Stdout.Write([]byte{'\r', '\n'})
				} else if ch == 3 {
					readingCmd = false
					atStartOfLine = true
					os.Stdout.Write([]byte("^C\r\n"))
				} else if ch == 24 {
					readingCmd = false
					atStartOfLine = true
					os.Stdout.Write([]byte("^X\r\n"))
				} else if ch == 8 || ch == 127 {
					if len(lineBuf) > 0 {
						lineBuf = lineBuf[:len(lineBuf)-1]
						os.Stdout.Write([]byte{8, ' ', 8})
					}
				} else if ch >= 32 && ch <= 126 {
					lineBuf = append(lineBuf, ch)
					os.Stdout.Write([]byte{ch})
				}
				continue
			}
		}

		if atStartOfLine && ch == '~' {
			readingCmd = true
			lineBuf = []byte{}
			os.Stdout.Write([]byte{'~'})
			continue
		}
		atStartOfLine = (ch == '\r' || ch == '\n')

		sendKey(ch)
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

	if runtime.GOOS != "windows" {
		RestoreSttyState()
	}

	if the_ram != nil {
		the_ram.Dump()
	}

	fmt.Printf("*** SHUTDOWN\n")
	fmt.Fprintf(os.Stderr, "*** SHUTDOWN\n")
	debug.PrintStack()
	os.Exit(13)
}

type ActiveSerial struct {
	Options OpenSerialOptions
	In      chan []byte
	Out     chan byte
}

func NewActiveSerial(options OpenSerialOptions) *ActiveSerial {
	uc := &ActiveSerial{
		Options: options,
		In:      make(chan []byte, 1024),
		Out:     make(chan byte, 1024),
	}
	go uc.loop()
	return uc
}

func (uc *ActiveSerial) loop() {
	var wasOpen bool
	for {
		serialPort, err := OpenSerial(uc.Options)
		if err != nil {
			if wasOpen {
				log.Printf("UsbClosed")
				wasOpen = false
			}
			time.Sleep(200 * time.Millisecond)
			continue
		}

		if !wasOpen {
			log.Printf("UsbOpen")
			wasOpen = true
		}

		done := make(chan struct{})

		// Reader goroutine
		go func() {
			buf := make([]byte, 1024)
			for {
				n, err := serialPort.Read(buf)
				if err != nil {
					close(done)
					return
				}
				for i := 0; i < n; i++ {
					uc.Out <- buf[i]
				}
			}
		}()

		// Writer
		// 1. send a raw 0 COBS mark to abort any partial packet in progress
		serialPort.Write([]byte{0x00})

		// 2. Send a T_HELLO packet { 178, 129, 0 } and a terminating raw 0 COBS mark
		helloEncoded := cobs.Encode([]byte{T_HELLO, 129, 0})
		helloEncoded = append(helloEncoded, 0x00)
		serialPort.Write(helloEncoded)

	WRITER:
		for {
			select {
			case <-done:
				// Connection lost from reader's perspective
				break WRITER
			case packet := <-uc.In:
				encoded := cobs.Encode(packet)
				encoded = append(encoded, 0x00)
				_, err := serialPort.Write(encoded)
				if err != nil {
					// Write failed, connection lost
					break WRITER
				}
			}
		}

		serialPort.Close()
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
		loadArgs = nil // Centipede does not load over USB yet
	}

	var previousPutChar byte
	var remember int64
	var timer_sum int64
	var timer_count int64
	pending := make(map[string]*EventRec)

	// COBS Decoder
	cobsChan := make(chan []byte, 100)
	go func() {
		var currentPacket []byte
		for b := range fromUSB {
			if b == 0 {
				if len(currentPacket) > 0 {
					decoded, err := cobs.Decode(currentPacket)
					if err == nil {
						if *TETHER_LOG_USB {
							ser := atomic.AddUint64(&tetherLogUsbSerial, 1)
							log.Printf("_%d COBS IN: len=%d %x", ser, len(decoded), decoded)
						}
						cobsChan <- decoded
					} else {
						log.Printf("COBS decode err: %v", err)
					}
					currentPacket = nil
				}
			} else {
				currentPacket = append(currentPacket, b)
			}
		}
	}()

	// gap := 1 // was for C_KEY, C_NOKEY
	for {
		select {
		case cmd := <-cmdChan:
			packet := append([]byte{T_COMMAND}, []byte(cmd)...)
			packet = append(packet, 0)
			WriteBytes(channelToPico, packet...)

		case inchar := <-inkey: // SELECT CASE user typed a character
			if inchar >= 1 {
				WriteBytes(channelToPico, inchar)
			}

		case pkt := <-cobsChan: // SELECT CASE Pico sent a COBS packet over the USB.
			if len(pkt) == 0 {
				continue
			}
			cmd := pkt[0]
			logGetByte(cmd, "cmd")

			pktPos := 1
			getByte := func(reason string) byte {
				if pktPos < len(pkt) {
					b := pkt[pktPos]
					logGetByte(b, reason)
					pktPos++
					return b
				}
				logGetByte(0, "EOF")
				return 0
			}

			bogus := 0

			// Write cycle diagnostic tracking
			var prevWriteCounterLSB byte
			var prevPushFailLSB byte
			var numWritesInPrevPacket int
			prevWriteCounterValid := false

			var ch byte // Used by default and C_PUTCHAR

			ReadCycleFunction := func(_addr uint, _data byte) {
				const GLOSS = 2
				Cycle++

				/*
					modName, modOffset := person.MemoryModuleOf(_addr)
					aline := Format("%q+%04x %s", modName, modOffset, AsmSourceLine(modName, modOffset))
				*/
				var aline string
				var ok_src bool
				if GLOSS > 0 {
					aline, ok_src = LinkSrc.Src[_addr]

					if GLOSS > 1 && ok_src {
						disasm, numBytes, numCycles, cycleCodes, ok := lib.Decode(the_ram.GetTrackRam()[_addr:])
						if ok {
							aline += Format(" ((%q %d,%d %q))", disasm, numBytes, numCycles, cycleCodes)
						}
					}
				}

				aline = strings.ReplaceAll(aline, "{;*;;}", "")  // seeing empty stuff is not useful.
				aline = strings.ReplaceAll(aline, "(     ", "(") // seeing empty stuff is not useful.
				aline = strings.ReplaceAll(aline, "(   ", "(")   // seeing empty stuff is not useful.
				aline = strings.ReplaceAll(aline, "(  ", "(")    // seeing empty stuff is not useful.
				aline = strings.ReplaceAll(aline, "( ", "(")     // seeing empty stuff is not useful.
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
							key := Format("%04x_%04x", _addr-12, ((uint(RTIHistory[10]) << 8) | uint(RTIHistory[11]) - 3))
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

			WriteCycleFunction := func(_addr uint, _data byte) {
				Cycle++

				the_ram.Poke1(_addr, _data)
				gloss := "   "
				switch _data >> 5 {
				case 0:
					gloss = Format("'%c'#", 64+(31&_data))
				case 1:
					gloss = Format("'%c'#", 32+(31&_data))
				case 2:
					gloss = Format("'%c'", 64+(31&_data))
				case 3:
					gloss = Format("'%c'", 32+(31&_data))
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
				cline := Format("cy-w %04x <- %02x    #%d  %s", _addr, _data, Cycle, gloss)
				Logf("%s", cline)
				if explain {
					ExplainOs9Call(_addr, _data, Swi2Num)
				}
			}

			switch cmd {

			case C_NOP:
				// NO OP.
				Logf("C_NOP")

			case T_RPC:
				pack := pkt[1:]
				HandleRpc(pack, channelToPico)

			case T_PICO_RPC:
				pack := pkt[1:]
				HandlePicoRpcResponse(pack)

			case C_RAM_CONFIG:
				pack := pkt[1:]
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
						Panicf("C_RAM_CONFIG size %d unknown value: % 3x", len(pack), pack)
						panic(0)
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
					Panicf("C_RAM_CONFIG unknown size %d: % 3x", len(pack), pack)
					panic(0)
				}

			case C_CYCLE:
				// TFR911 uncompressed cycle format with FIC marking.
				pack := pkt[1:]
				HandleCCycle(pack, person, channelToPico)

			case C_COMPRESSED_CYCLES: // 175
				// Packet format: [cmd, numCycles, write_counter_lsb, push_fail_lsb, compressed...]
				numCycles := int(pkt[1])
				writeCounterLSB := pkt[2]
				pushFailLSB := pkt[3]

				// Detect write counter gaps (wraps at 256)
				if prevWriteCounterValid {
					expected := (prevWriteCounterLSB + byte(numWritesInPrevPacket)) & 0xFF
					if writeCounterLSB != expected {
						gap := int(writeCounterLSB) - int(expected)
						if gap < 0 {
							gap += 256
						}
						log.Printf("WARNING: write counter gap! expected=%d got=%d (lost ~%d writes)", expected, writeCounterLSB, gap)
					}
				}
				if pushFailLSB != prevPushFailLSB {
					log.Printf("WARNING: push failures detected! push_fail_counter LSB=%d (was %d)", pushFailLSB, prevPushFailLSB)
					prevPushFailLSB = pushFailLSB
				}

				// Count writes in this packet for next gap check
				numWritesInPacket := 0
				pack := pkt[4:]
				cycles := DecompressCycles(pack, numCycles)
				for _, cy := range cycles {
					direction, addr, data := (cy>>24)&0xFF, (cy>>8)&0xFFFF, cy&0xFF
					switch direction {
					case 1: // read cycle
						ReadCycleFunction(uint(addr), byte(data))
					case 3: // write cycle
						numWritesInPacket++
						WriteCycleFunction(uint(addr), byte(data))
					default:
						Panicf("Bad direction in DecompressCycles: %x %x %x", direction, addr, data)
						panic(0)
					}
				}
				prevWriteCounterLSB = writeCounterLSB
				numWritesInPrevPacket = numWritesInPacket
				prevWriteCounterValid = true

			case C_READ_CYCLE: // centipede: A A D
				pack := pkt[1:]
				if len(pack) == 3 {
					if *CENTIPEDE {
						_addr := (uint(pack[0]) << 8) + uint(pack[1])
						_data := pack[2]

						// aline, _ := LinkSrc.Src[_addr]
						// Logf("%04x r %02x %s", _addr, _data, aline)

						ReadCycleFunction(_addr, _data)
					}
				}

			case C_FIC_CYCLE: // TFR911: First Instruction Cycle: A A D
				pack := pkt[1:]
				if len(pack) == 3 {
					_addr := (uint(pack[0]) << 8) + uint(pack[1])
					_data := pack[2]
					Cycle++

					// Source assembly lookup
					var srcInfo string
					if person != nil {
						modName, modOffset := person.MemoryModuleOf(_addr)
						if modName != "" {
							srcInfo = Format(":%q+%04x %s", modName, modOffset, AsmSourceLine(modName, modOffset))
						}
					}
					if srcInfo == "" && LinkSrc != nil {
						if aline, ok := LinkSrc.Src[_addr]; ok {
							srcInfo = aline
						}
					}

					// Disassembly
					disasm := ""
					trackRam := the_ram.GetTrackRam()
					if trackRam != nil && _addr < uint(len(trackRam)) {
						if d, _, _, _, ok := lib.Decode(trackRam[_addr:]); ok {
							disasm = d
						}
					}

					Logf("cy-F %04x   -> %02x  #%d  %s  %s", _addr, _data, Cycle, disasm, srcInfo)
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
				pack := pkt[1:]
				Logf("LOG[%d]: %q", cmd-C_LOGGING, pack)

			case C_DISK_WRITE:
				//Logf("C_DISK_WRITE[%d]: ...", 111)
				pack := pkt[1:]
				//Logf("C_DISK_WRITE[%d]: %q ...", 222, pack)
				EmulateDiskWrite(pack, channelToPico)
				//Logf("C_DISK_WRITE[%d]: %q", 333, pack)

			case C_DISK_READ:
				pack := pkt[1:]
				EmulateDiskRead(pack, channelToPico)

			case C_EVENT:
				pack := pkt[1:]
				OnEvent(pack, pending, person)

			case C_RAM3_WRITE:
				Panicf("%v", "C_RAM3_WRITE not imp")
				panic(0)

			case C_RAM5_WRITE:
				pack := pkt[1:]
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

			case C_WRITE_CYCLE:
				pack := pkt[1:]
				AssertEQ(len(pack), 3)

				hi := pack[0]
				lo := pack[1]
				addr := (uint(hi) << 8) | uint(lo)

				data := pack[2]
				//fmt.Printf("W %04x %02x\n", addr, data)
				// fmt.Printf("^");

				if *CENTIPEDE {
					_data := pack[2]
					_addr := (uint(pack[0]) << 8) + uint(pack[1])
					WriteCycleFunction(_addr, _data)
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
					what := getByte("C_DUMP_START")
					switch what {
					case C_DUMP_LINE:
						a := getByte("a")
						b := getByte("b")
						c := getByte("c")
						var d [16]byte
						for j := uint(0); j < 16; j++ {
							d[j] = getByte("d")
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
					if true {
						log.Printf("Unexpected cmd byte: $%02x == $d.", cmd, cmd)
						fmt.Printf("[x%02x]", cmd)
					} else {
						log.Printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ X")
						debug.PrintStack()
						log.Printf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Y")
						Panicf("cmd == %d", cmd)
						panic(0)
						Panicf("%v", cmd)
						panic(0)
					}
				}
				fallthrough

			case C_PUTCHAR:
				var chs []byte
				if cmd == C_PUTCHAR {
					for pktPos < len(pkt) {
						chs = append(chs, getByte("C_PUTCHAR"))
					}
				} else {
					chs = append(chs, ch)
				} // otherwise use the ch from default case.

				for _, ch = range chs {
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
						if loadArgs != nil {
							if LookForPreSync(ch) {
								// Will send over wire
								PreUploadArgs(loadArgs, channelToPico)
								loadArgs = nil // now LOAD is empty, so we don't load again.
							}
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
				}

			case C_SHUTDOWN:
				fmt.Printf("\n[255: shutdown]\n")
				Logf("go func: Received C_SHUTDOWN; exiting")
				close(channelFromPico)
				Panicf("go func: C_SHUTDOWN")
				panic(0)
				return

			} // end switch cmd
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
	activeSerial := NewActiveSerial(serialOptions)
	channelToPico := activeSerial.In
	channelFromPico := activeSerial.Out
	var fromUSB <-chan byte = channelFromPico

	if *QUICK_INJECT != "" {
		go func() {
			log.Printf("STARTING QUICK-INJECT: %q", *QUICK_INJECT)
			resp, err := PicoRpcCall(channelToPico, "inject", []byte(*QUICK_INJECT), 60*time.Second)
			if err != nil {
				if tmpDirToClean != "" { os.RemoveAll(tmpDirToClean) }
				log.Fatalf("QUICK-INJECT FAILED: %v", err)
			}
			if resp.Status != 0 {
				if tmpDirToClean != "" { os.RemoveAll(tmpDirToClean) }
				log.Printf("QUICK-INJECT ERROR: STATUS=%d RESULT=%q", resp.Status, string(resp.Data))
				os.Exit(1)
			} else {
				if tmpDirToClean != "" { os.RemoveAll(tmpDirToClean) }
				log.Printf("QUICK-INJECT SUCCESS")
				fmt.Printf("%s\n", string(resp.Data))
				os.Exit(0)
			}
		}()
	}

	if *CENTIPEDE {
		// Will load into tether's the_ram
		PreUploadArgs(flag.Args(), channelToPico)
	}

	go TextDaemon()

	// RunSelect blocks forever (or until shutdown/panic)
	RunSelect(inkey, fromUSB, channelToPico, channelFromPico, person)
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
	CY_FIC // first instruction cycle (TFR911's HD63C09EP LIC pin)
)

var LogLimit = flag.Uint64("logmax", 1<<30, "maximum bytes to log to stderr")

type LimitedLogWriter struct {
	Limit   uint64
	Current uint64
}

type IgnoreWriter struct {}

func (w IgnoreWriter) Write(bb []byte) (int, error) {return len(bb), nil}

func InstallLimitedLogWriter() {
    if *OMIT_STDERR {
        log.SetOutput(&IgnoreWriter{})
    } else if *LogLimit > 0 {
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
	return false
	//XX// copy(syncWindow[0:3], syncWindow[1:4])
	//XX// syncWindow[3] = ch
	//XX// Logf("LookForPreSync: %q vs %q", syncWindow[:], ".:,;")
	//XX// return string(syncWindow[:]) == ".:,;"
}

func ExplainOs9Call(_addr uint, _data byte, os9num byte) {
    /*
	defer func() {
		r := recover()
		if r != nil {
			Logf("\nCAUGHT PANIC during ExplainOs9Call a=%x d=%x os9=%x: %v", _addr, _data, os9num, r)
		}
	}()

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
    */
}
