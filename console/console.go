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

	putchars := make(chan byte, 1024*1024)

	go func() {
		for {
			v := make([]byte, 100)
			n, err := port.Read(v)
			if err != nil {
				log.Fatalf("port.Read: %v", err)
			}
			log.Printf("Read %d bytes: %q", n, v[:n])

			for i := 0; i < n; i++ {
				switch v[i] {
				case C_PUTCHAR:
					i++
					ch := v[i]
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
					return
				default:
				LOGGING:
					for i+1 < n {
						i++
						ch := v[i]
						var bb bytes.Buffer
						switch {
						case 32 <= ch && ch <= 126:
							bb.WriteByte(ch)
						case ch == 10 || ch == 13:
							log.Printf("LOG %q", bb.String())
							break LOGGING
						default:
							fmt.Fprintf(&bb, "{%d}", ch)
						}
					}
				}
			}
		}
	}()

	for {
		v := make([]byte, 1024)
		n, err := port.Read(v)
		if err != nil {
			log.Fatalf("port.Read: %v", err)
		}
		log.Printf("Read %d bytes: %q", n, v[:n])

		var p1, p2 byte
		for i := 0; i < n; i++ {
			x := v[i]
			if x == 255 && p1 == 255 && p2 == 255 {
				log.Printf("main: Triple 255: exiting")
				return
			}
			putchars <- x
			p2 = p1
			p1 = x
		}
	}
}
