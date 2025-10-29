package main

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"time"
)

var VDG_TEXT = flag.Bool("vdg_text", false, "whether to show VDG text")

var SamBits uint
var Pia1OutB byte

func SamModeV() uint         { return 7 & SamBits }
func SamModeF() uint         { return 127 & (SamBits >> 3) }
func SamModeP() uint         { return 1 & (SamBits >> 10) }
func SamModeR() uint         { return 3 & (SamBits >> 11) }
func SamModeM() uint         { return 3 & (SamBits >> 13) }
func SamModeTY() uint        { return 1 & (SamBits >> 15) }
func SamScreenAddress() uint { return SamModeF() << 9 }

func ScanTextContents() []byte {
	if SamModeV() != 0 {
		return nil
	}

	// base := SamScreenAddress()
	const base = 0x0400
	z := make([]byte, 512)
	ram := the_ram.GetTrackRam()
	for i := uint(0); i < 512; i++ {
		ch := ram[base+i]
		if 0 == (ch & 0x80) {
			a := ch & 63
			if a < 32 {
				a += 64
			}
			z[i] = a
		} else {
			if 0 == (ch & 15) {
				z[i] = '_'
			} else {
				z[i] = '#'
			}
		}
	}
	return z
}

var tOld []byte

func ScanTextUpdate() ([]byte, bool) {
	tNew := ScanTextContents()
	if len(tNew) != len(tOld) {
		tOld = tNew
		return tNew, true
	}
	for i := 0; i < len(tNew); i++ {
		if tOld[i] != tNew[i] {
			tOld = tNew
			return tNew, true
		}
	}
	return nil, false
}

func TextTick() {
	txt, ok := ScanTextUpdate()
	if ok && len(txt) == 512 {
		var buf bytes.Buffer
		fmt.Fprintf(&buf, "\n________________________________\n")

		for y := 0; y < 512; y += 32 {
			for x := 0; x < 32; x++ {
				ch := txt[x+y]
				buf.WriteByte(ch)
			}
			buf.WriteByte('\n')
		}
		fmt.Fprintf(&buf, "--------------------------------\n")
		os.Stdout.Write(buf.Bytes())
	}
}

const TextScreenCheckIntervalMS = 100

func TextDaemon() {
	if *VDG_TEXT {
		for {
			time.Sleep(TextScreenCheckIntervalMS * time.Millisecond)
			TextTick()
		}
	}
}
