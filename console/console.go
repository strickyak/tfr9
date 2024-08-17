package main

import (
    "flag"
	"fmt"
	"log"

	"github.com/jacobsa/go-serial/serial"
)

var TTY = flag.String("tty", "/dev/ttyACM0", "serial device connected by USB to Pi Pico")
var BAUD = flag.Uint("baud", 115200, "serial device baud rate")

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

	// Write 4 bytes to the port.
	b := []byte("WXYZ")
	n, err := port.Write(b)
	if err != nil {
		log.Fatalf("port.Write: %v", err)
	}
	fmt.Printf("Wrote %d bytes.", n)

    v := make([]byte, 100)
	n, err = port.Read(v)
	if err != nil {
		log.Fatalf("port.Read: %v", err)
	}
	fmt.Printf("Read %d bytes: %q", n, v[:n])
}
