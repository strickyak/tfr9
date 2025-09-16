package main

import (
	"bufio"
	"bytes"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
)

func PreUpload(commaList string, channelToPico chan []byte) {
	words := strings.Split(commaList, ",")
	for _, w := range words {
		Logf("Upload Word: %q", w)
		if strings.HasPrefix(w, "decb:") {

			w = strings.TrimPrefix(w, "decb:")
			PreUploadDecb(w, channelToPico)

		} else if strings.HasPrefix(w, "srec:") {

			w = strings.TrimPrefix(w, "srec:")
			PreUploadSrec(w, channelToPico)

		} else if strings.HasPrefix(w, "rom:") {

			w := strings.TrimPrefix(w, "rom:")
			ht := strings.Split(w, ":")
			if len(ht) != 2 {
				Panicf("Expected rom:address:filename in %q", w)
			}
			h, t := ht[0], ht[1]
            addr := SmartAtoi(h, 16)
			PreUploadRom(t, channelToPico, addr)
		} else if strings.HasSuffix(w, ".decb") {
            PreUpload("decb:" + w, channelToPico)
		} else if strings.HasSuffix(w, ".srec") {
            PreUpload("srec:" + w, channelToPico)
		} else if strings.HasSuffix(w, ".rom") {
            PreUpload("rom:" + w, channelToPico)
		} else {
			Panicf("Missing prefix before filename: %q (expected 'decb:' or 'rom:' or 'srec:')", w)
		}
	}
	Logf("PreUpload: end")
}

func PreUploadRom(filename string, channelToPico chan []byte, addr uint) {
	defer func() {
		r := recover()
		if r != nil {
			log.Panicf("Error during PreUploadRom(%q): %v", filename, r)
		}
	}()

	syncWindow = [4]byte{0, 0, 0, 0} // Undo pattern.

	bb, err := os.ReadFile(filename)
	if err != nil {
		log.Panicf("cannot ReadFile %q: %v", filename, err)
	}

	for len(bb) > 0 {
		n := uint(len(bb))
		if n > 60 {
			n = 60 // max 60 at a time, plus 2-byte addr
		}

		out := make([]byte, n+4)
		out[0] = C_PRE_LOAD
		out[1] = byte(128 + n + 2)
		out[2] = byte(addr >> 8)
		out[3] = byte(addr & 255)
		copy(out[4:], bb[:n])
		Logf("PreUploadRom: n=%d. a=%x d= { % 3x }", n, addr, bb[:n])
		WriteBytes(channelToPico, out...)

		bb = bb[n:]
		addr += n
	}
}

func PreUploadDecb(filename string, channelToPico chan []byte) {
	defer func() {
		r := recover()
		if r != nil {
			log.Panicf("Error during PreUploadDecb(%q): %v", filename, r)
		}
	}()

	syncWindow = [4]byte{0, 0, 0, 0} // Undo pattern.

	bb, err := os.ReadFile(filename)
	if err != nil {
		log.Panicf("cannot ReadFile %q: %v", filename, err)
	}
LOOP:
	for len(bb) > 0 {
		switch bb[0] {
		case 0: // block of data to poke
			sz := (uint(bb[1]) << 8) + uint(bb[2])
			addr := (uint(bb[3]) << 8) + uint(bb[4])
			bb = bb[5:]
			for sz > 0 {
				n := sz
				if n > 60 {
					n = 60 // max 60 at a time, plus 2-byte addr
				}
				out := make([]byte, n+4)
				out[0] = C_PRE_LOAD
				out[1] = byte(128 + n + 2)
				out[2] = byte(addr >> 8)
				out[3] = byte(addr & 255)
				copy(out[4:], bb[:n])
				Logf("PreUploadDecb: n=%d. a=%x d= { % 3x }", n, addr, bb[:n])
				WriteBytes(channelToPico, out...)
				sz -= n
				bb = bb[n:]
				addr += n
			}
		case 0xFF: // final block with start address
			// Load the start value in the reset vector.
			addr := (uint(bb[3]) << 8) + uint(bb[4])

            if addr != 0 {
			    // 0xFFFE is the address of the reset vector.
			    Logf("PreUploadDecb: reset vector is %x", addr)
			    WriteBytes(channelToPico, C_PRE_LOAD, 128+4, 0xFF, 0xFE, byte(addr>>8), byte(addr&255))
            }

			bb = bb[5:]

			break LOOP // fuzix.bin has trailing zeros that would confuse us (e.g. "runtime error: index out of range [2] with length 2")

		default:
			log.Panicf("bad control byte $%x, which is %d bytes from end", bb[0], len(bb))
		}
	}
	Logf("PreUploadDecb: end while")
	LOAD = new(string) // now LOAD points to an empty string, so we don't load again.
}

func PreUploadSrec(filename string, channelToPico chan []byte) {
	defer func() {
		r := recover()
		if r != nil {
			log.Panicf("Error during PreUploadSrec(%q): %v", filename, r)
		}
	}()

	syncWindow = [4]byte{0, 0, 0, 0} // Undo pattern.

	bb, err := os.ReadFile(filename)
	if err != nil {
		log.Panicf("cannot ReadFile %q: %v", filename, err)
	}
	for i, b := range bb {
		if b == '\r' {
			bb[i] = '\n' // Use newlines, not CRs
		}
	}

	r := bytes.NewBuffer(bb)
	scanner := bufio.NewScanner(r)

	for scanner.Scan() {
		line := scanner.Text()
		Logf("SREC: line %q", line)
		rec := DecodeSLine(line)
		if rec == nil {
			Logf("=-=-= nil")
		} else if rec.typenum == '1' {
			Logf("SREC[1] %v", *rec)
			n := len(rec.data)
			out := make([]byte, n+4)
			out[0] = C_PRE_LOAD
			out[1] = byte(128 + n + 2)
			out[2] = byte(rec.addr >> 8)
			out[3] = byte(rec.addr & 255)
			copy(out[4:], rec.data[:n])
			Logf("PreUpload: n=%d. a=%x d= { % 3x }", n, rec.addr, rec.data[:n])
			WriteBytes(channelToPico, out...)
		} else if rec.typenum == '9' {
			Logf("SREC[9] %v", *rec)

            if rec.addr != 0 {
			    // 0xFFFE is the address of the reset vector.
			    Logf("PreUploadSrec: reset vector is %x", rec.addr)
			    WriteBytes(channelToPico, C_PRE_LOAD, 128+4, 0xFF, 0xFE, byte(rec.addr>>8), byte(rec.addr&255))
            }
        }
	}

	if scanErr := scanner.Err(); scanErr != nil {
		fmt.Println("Error reading file %q: ", filename, scanErr)
	}

	// TODO -- write ram
}

// https://upload.wikimedia.org/wikipedia/commons/thumb/f/f1/Motorola_SREC_Chart.png/1200px-Motorola_SREC_Chart.png
type SRecord struct {
	typenum  byte
	addr     uint
	data     []byte
	checksum uint
}

func DecodeSLine(line string) *SRecord {
	if line == "" {
		return nil
	}
	if line[0] != 'S' {
		return nil
	}
	if line[1] != '1' && line[1] != '9' {
		return nil
    }
	fmt.Printf("S")
	defer func() {
		r := recover()
		if r != nil {
			Panicf("Error in DecodeSLine(%q): %v", line, r)
		}
	}()
	count, err := strconv.ParseUint(line[2:4], 16, 8)
	if err != nil {
		Panicf("Bad count field %q in SRecord %q", line[2:4], line)
	}
	addr, err := strconv.ParseUint(line[4:8], 16, 16)
	if err != nil {
		Panicf("Bad address field %q in SRecord %q", line[2:4], line)
	}
	size := uint(count) - 3 // size of data is count less 2 for addr and 1 for checksum

	data := make([]byte, size)
	for i := uint(0); i < size; i++ {
		datum, err := strconv.ParseUint(line[8+2*i:8+2*i+2], 16, 8)
		if err != nil {
			Panicf("Bad data field %q, index %d, in SRecord %q", line[8:8+2*size], i, line)
		}
		data[i] = byte(datum)
	}

	checksum, err := strconv.ParseUint(line[8+2*size:8+2*size+2], 16, 8)
	if err != nil {
		Panicf("Bad address field %q in SRecord %q", line[2:4], line)
	}

	sum := byte(0)
	for i := uint64(0); i < count; i++ {
		datum, err := strconv.ParseUint(line[2+2*i:2+2*i+2], 16, 8)
		if err != nil {
			Panicf("Bad field during checksum at index %d, in SRecord %q", i, line)
		}
		sum += byte(datum)
	}
	Logf("checksum %x sum %x", checksum, ^sum)

	return &SRecord{
		typenum:  line[1],
		addr:     uint(addr),
		data:     data,
		checksum: uint(checksum),
	}
}
