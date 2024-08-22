package main

import (
	"bytes"
	"flag"
	"fmt"
	"log"
	"time"

	"github.com/jacobsa/go-serial/serial"
)

var TTY = flag.String("tty", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")

const (
	C_PUTCHAR = 161
)

func Once() {
	// Set up options.
	options := serial.OpenOptions{
		PortName:        *TTY,
		BaudRate:        *BAUD,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

	// Open the port.
	port, err := serial.Open(options)
	if err != nil {
		fmt.Printf("\n[serial.Open: %q]\n", err)
		log.Panicf("serial.Open: %v", err)
	}

	// Make sure to close it later.
	defer port.Close()

	if false {
		// Write 4 bytes to the port.
		b := []byte("WXYZ")
		n, err := port.Write(b)
		if err != nil {
			fmt.Printf("\n[port.Write: %q]\n", err)
			log.Panicf("port.Write: %v", err)
		}
		log.Printf("Wrote %d bytes.", n)
	}

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
			fmt.Printf("\n[port.Read: %q]\n", err)
			log.Panicf("port.Read: %v", err)
		}

		for i := 0; i < n; i++ {
			putchars <- v[i]
		}
	}
	log.Printf("End LOOP.")
}
func TryOnce() {
	defer func() {
		r := recover()
		if r != nil {
			fmt.Printf("[recover: %q]\n", r)
		}
	}()
	Once()
}
func main() {
	flag.Parse()
	for {
		TryOnce()
		time.Sleep(1 * time.Second)
	}
}
