package main

import (
	"bytes"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	"github.com/jacobsa/go-serial/serial"
)

var TTY = flag.String("tty", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")
var DISKS = flag.String("disks", "", "Comma-separated filepaths to disk files, in order of drive number")

const (
	C_PUTCHAR = 161
)

type DiskFiles []*os.File

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

func Once(files DiskFiles) {
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

	putchars := make(chan byte, 1024)

	go func() {
		var bb bytes.Buffer
		cr := false
		for {
			x, ok := <-putchars
			if !ok {
				break
			}
			switch x {
			case C_PUTCHAR:
				ch := <-putchars
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

			case 255:
				fmt.Printf("\n[255: finished]\n")
				log.Printf("go func: Received END MARK 255; exiting")
				close(putchars)
				return

			default:
				switch {
				case 32 <= x && x <= 126:
					bb.WriteByte(x)
				case x == 13:
					// do nothing
				case x == 10:
					log.Printf("# %s", bb.String())
					bb.Reset()
				default:
					fmt.Fprintf(&bb, "{%d}", x)
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
			putchars <- v[i]
		}
	}
	log.Printf("End LOOP.")
}
func TryOnce(files DiskFiles) {
	defer func() {
		r := recover()
		if r != nil {
			fmt.Printf("[recover: %q]\n", r)
		}
	}()
	Once(files)
}
func main() {
    log.SetFlags(0)
    log.SetPrefix("!")
	flag.Parse()

	files := OpenDisks(*DISKS)
	for {
		TryOnce(files)
		time.Sleep(1 * time.Second)
	}
}
