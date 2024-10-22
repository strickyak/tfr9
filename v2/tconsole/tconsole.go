package main

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"os/exec"
	"strings"
	"syscall"
	"time"

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
)

var NormalKeys = "@ABCDEFG" + "HIJKLMNO" + "PQRSTUVW" + "XYZ^\n\b\t " + "01234567" + "89:;,-./" + "\r\014\003"
var ShiftedKeys = "@abcdefg" + "hijklmno" + "pqrstuvw" + "xyz^\n\b\t " + "\177!\"#$%&'" + "()*+<=>?" + "\r\014\003"

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

func Panicf(format string, args ...any) {
	// fmt.Printf("\n[[[ "+format+" ]]]\n", args...)
	log.Panicf("PANIC: "+format+"\n", args...)
}

// getByte from USB channel, for Binary Data
func getByte(fromUSB <-chan byte) byte {
	a := <-fromUSB
	if a == 1 { // 1 is the escape char
		b := <-fromUSB
		return b - 32
	}
	return a
}

func WriteBytes(usbout chan []byte, vec ...byte) {
    usbout <- vec
}
func XXXXXXXXXXXXXXXWriteBytes(w io.Writer, vec ...byte) {
    bb := make([]byte, 1)
    for _, x := range vec {
        bb[0] = x
	    w.Write(bb)
        time.Sleep(1 * time.Millisecond)
    }
	//w.Write(vec)
}

var cr bool

func Once(files DiskFiles, inkey chan byte) {
	// Set up options for Serial Port.
	options := serial.OpenOptions{
		PortName:        *TTY,
		BaudRate:        *BAUD,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

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
		var bb bytes.Buffer
		stop := false
		gap := 1
FOR:
		for {


            inchar, incharOK := TryInkey(inkey)
            if incharOK {
			    // WriteBytes(port, inchar)
			    WriteBytes(usbout, inchar)
		        // time.Sleep(100 * time.Millisecond)
			}


            var cmd byte
            select {
            case usb, ok := <- fromUSB:
                if !ok {
                    break FOR
                }
                cmd = usb
            default:
                continue FOR
            }


			//cmd, ok := <-fromUSB
			//if !ok {
				//break
			//}


			switch cmd {
			case C_DUMP_RAM:
				log.Printf("{{{ RamDump")
DUMPING:
				for {
					what := getByte(fromUSB)
					switch what {
					case C_DUMP_LINE:
						a := getByte(fromUSB)
						b := getByte(fromUSB)
						var d [16]byte
						for j := 0; j < 16; j++ {
							d[j] = getByte(fromUSB)
						}

						var buf bytes.Buffer
						fmt.Fprintf(&buf, ":%04x: ", (uint(a)<<8 | uint(b)))
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
						panic(what)
					}
				}
				log.Printf("}}} RamDump")

			case C_PUTCHAR:
				ch := getByte(fromUSB)
				switch {
				case 32 <= ch && ch <= 126:
					fmt.Printf("%c", ch)
					cr = false
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
					log.Printf("# %s", bb.String())
					bb.Reset()
				case cmd == 10:
					// fmt.Fprintf(&bb, "{%d}", cmd)
				default:
					fmt.Fprintf(&bb, "{%d}", cmd)
				}
			}
			if bb.Len() > 250 {
				log.Printf("# %q\\", bb.String())
				bb.Reset()
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
func TryOnce(files DiskFiles, inkey chan byte) {
	defer func() {
		r := recover()
		if r != nil {
			fmt.Printf("[recover: %q]\n", r)
		}
	}()
	Once(files, inkey)
}
func main() {
	log.SetFlags(0)
	log.SetPrefix("!")
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
		TryOnce(files, inkey)
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
    case x := <- inkey:
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
