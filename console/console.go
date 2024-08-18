package main

import (
	"bytes"
	"flag"
	"fmt"
	"log"

	"github.com/jacobsa/go-serial/serial"
)

var TTY = flag.String("tty", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")

const (
	C_PUTCHAR = 161
)

func main() {
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
		log.Fatalf("serial.Open: %v", err)
	}

	// Make sure to close it later.
	defer port.Close()

	if false {
		// Write 4 bytes to the port.
		b := []byte("WXYZ")
		n, err := port.Write(b)
		if err != nil {
			log.Fatalf("port.Write: %v", err)
		}
		log.Printf("Wrote %d bytes.", n)
	}

	putchars := make(chan byte, 1024)

	go func() {
		var bb bytes.Buffer
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
				case ch == 10 || ch == 13:
					fmt.Println()
				default:
					fmt.Printf("{%d}", ch)
				}

			case 255:
				log.Printf("go func: Received END MARK 255; exiting")
				close(putchars)
				return
			default:
				switch {
				case 32 <= x && x <= 126:
					bb.WriteByte(x)
				case x == 10:
					bb.WriteByte(10)
				default:
					fmt.Fprintf(&bb, "{%d}", x)
				}
			}
			if bb.Len() > 63 {
				log.Printf("LOG %q", bb.String())
				bb.Reset()
			}
		}
	}()

	for {
		v := make([]byte, 1024)
		n, err := port.Read(v)
		if err != nil {
			log.Fatalf("port.Read: %v", err)
		}
		// log.Printf("Read %d bytes: %q", n, v[:n])

		//var p1, p2 byte
		for i := 0; i < n; i++ {
			x := v[i]
			putchars <- x
		}
	}
	log.Printf("End LOOP.")
}
