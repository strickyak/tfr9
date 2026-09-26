package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"time"
)

var VDG_TEXT = flag.Bool("vdg_text", false, "whether to show VDG text")

var SamBits uint

func SamModeV() uint         { return 7 & SamBits }
func SamModeF() uint         { return 127 & (SamBits >> 3) }
func SamModeP() uint         { return 1 & (SamBits >> 10) }
func SamModeR() uint         { return 3 & (SamBits >> 11) }
func SamModeM() uint         { return 3 & (SamBits >> 13) }
func SamModeTY() uint        { return 1 & (SamBits >> 15) }
func SamScreenAddress() uint { return SamModeF() << 9 }

func Pia1OutB() byte {
	return the_ram.Peek1(0xFF23) // assuming write DATA after writing DIRECTION
}

func SamPoke1(addr uint) {
	if 0xFFC0 <= addr && addr < 0xFFE0 {
		a := addr - 0xFFC0
		bitNum := a >> 1
		if (a & 1) == 1 {
			// Set the SAM bit
			SamBits |= (1 << bitNum)
		} else {
			// Clear the SAM bit
			SamBits &^= (1 << bitNum)
		}
	}
}

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

/*
const (
    VGA_TEXT = 1
    PMODE_1 = 2
)

type ScreenContents struct {
    Mode int
    ColorSet int
    Guts []byte
}
*/

func GetScreenForWebsocket() []byte {
	fb := SamScreenAddress()
	Logf("SAM V=%d addr=$%04x P1B=$%02x", SamModeV(), fb, Pia1OutB())

	switch SamModeV() {
	case 0: // Text
		return GetTextScreen(fb)
	case 4: // PMODE 1
		return GetPmode1Screen(fb)
		// case 6: // PMODE 3 : SAM V=6 addr=$0600 P1B=$37 // TODO -- look this up
	case 6: // PMODE 4
		return GetPmode4Screen(fb)
	}
	return nil
}

func GetPmode1Screen(base uint) []byte {
	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(0))
	binary.Write(&buf, binary.LittleEndian, uint16(0))
	binary.Write(&buf, binary.LittleEndian, uint16(128*2))
	binary.Write(&buf, binary.LittleEndian, uint16(96*2))

	colorBias := (Pia1OutB() & 8) >> 1 // 0 or 4

	p := base
	for y := uint(0); y < 96; y++ {
		for t := 0; t < 2; t++ {
			prep := p
			for x := uint(0); x < 128/4; x++ {
				b := the_ram.Peek1(p)
				p++
				for j := uint(0); j < 4; j++ {
					color := colorBias + 3&(b>>(6-(j+j)))
					rgb := VdgSemiGraphicsColors[color]
					buf.Write(rgb)
					buf.Write(rgb)
				}
			}
			if t == 0 {
				p = prep
			}
		}
	}
	return buf.Bytes()
}
func GetPmode3Screen(base uint) []byte { // TODO -- look this up.
	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(0))
	binary.Write(&buf, binary.LittleEndian, uint16(0))
	binary.Write(&buf, binary.LittleEndian, uint16(128*2))
	binary.Write(&buf, binary.LittleEndian, uint16(192))

	colorBias := (Pia1OutB() & 8) >> 1 // 0 or 4

	p := base
	for y := uint(0); y < 192; y++ {
		for x := uint(0); x < 128/4; x++ {
			b := the_ram.Peek1(p)
			p++
			for j := uint(0); j < 4; j++ {
				color := colorBias + 3&(b>>(6-(j+j)))
				rgb := VdgSemiGraphicsColors[color]
				buf.Write(rgb)
				buf.Write(rgb)
			}
		}
	}
	return buf.Bytes()
}

func GetPmode4Screen(base uint) []byte {
	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(0))
	binary.Write(&buf, binary.LittleEndian, uint16(0))
	binary.Write(&buf, binary.LittleEndian, uint16(256))
	binary.Write(&buf, binary.LittleEndian, uint16(192))

	colorBias := (Pia1OutB() & 8) >> 1 // 0 or 4

	p := base
	for y := uint(0); y < 192; y++ {
		for x := uint(0); x < 256/2; x++ {
			b := the_ram.Peek1(p)
			p++
			for j := uint(0); j < 8; j++ {
				if (1 & (b >> (7 - j))) != 0 {
					buf.Write(VdgSemiGraphicsColors[colorBias])
				} else {
					buf.Write([]byte{0, 0, 0}) // blackish
				}
			}
		}
	}
	return buf.Bytes()
}

func GetTextScreen(base uint) []byte {
	var buf bytes.Buffer

	p := base
	for y := uint(0); y < 16; y++ {
		for x := uint(0); x < 32; x++ {
			ch := the_ram.Peek1(p)
			p++

			if ch < 128 {
				// Text
				if ch == 32 {
					continue // dont draw blanks
				}
				invert := ch >= 64
				ch &= 63
				fi := 7 * uint(ch)

				buf.WriteByte(OpBitmap)
				binary.Write(&buf, binary.LittleEndian, uint16(8*x))
				binary.Write(&buf, binary.LittleEndian, uint16(12*y+uint(Cond(invert, 0, 3))))
				binary.Write(&buf, binary.LittleEndian, uint16(8))
				binary.Write(&buf, binary.LittleEndian, uint16(Cond(invert, 12, 7)))

				if invert {
					for i := uint(0); i < 3*8; i++ {
						buf.Write(VdgSemiGraphicsColors[0]) // green
					}
				}
				for fy := uint(0); fy < 7; fy++ {
					for fx := uint(0); fx < 8; fx++ {
						var pixel bool
						pixel = ((VdgFont[fi+fy] >> (7 - fx)) & 1) != 0
						if invert {
							pixel = !pixel
						}
						if pixel {
							buf.Write(VdgSemiGraphicsColors[0]) // green
						} else {
							buf.Write([]byte{0, 0, 0}) // blackish
						}
					}
				}
				if invert {
					for i := uint(0); i < 2*8; i++ {
						buf.Write(VdgSemiGraphicsColors[0]) // green
					}
				}
				/*
										for fx := uint(0); fx < 8; fx++ {
											pixel := false
											if invert {
												pixel = !pixel
											}
											if pixel {
												buf.Write(VdgSemiGraphicsColors[0]) // green
											} else {
												buf.Write([]byte{0, 0, 0}) // blackish
											}
					                    }
				*/
			} else {
				// Semi-Graphics
				if (ch & 15) == 0 {
					continue // Do not draw blank space
				}
				buf.WriteByte(OpBitmap)
				binary.Write(&buf, binary.LittleEndian, uint16(8*x))
				binary.Write(&buf, binary.LittleEndian, uint16(12*y))
				binary.Write(&buf, binary.LittleEndian, uint16(8))
				binary.Write(&buf, binary.LittleEndian, uint16(12))

				shape := 15 & ch
				color := 7 & (ch >> 4)
				rgb := VdgSemiGraphicsColors[color]
				for i := 0; i < 6; i++ {
					if (shape & 8) != 0 {
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
					} else {
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
					}
					if (shape & 4) != 0 {
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
					} else {
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
					}
				}
				for i := 0; i < 6; i++ {
					if (shape & 2) != 0 {
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
					} else {
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
					}
					if (shape & 1) != 0 {
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
						buf.Write(rgb)
					} else {
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
						buf.Write(BlackRGB)
					}
				}
			}

		}
	}
	return buf.Bytes()
}

var VdgSemiGraphicsColors = [][]byte{
	{50, 200, 50},   // green
	{230, 230, 0},   // yellow
	{0, 0, 250},     // blue
	{230, 0, 0},     // red
	{200, 200, 200}, // buff
	{50, 150, 250},  // lt blue
	{200, 50, 200},  // magenta
	{250, 140, 0},   // orange
}

var BlackRGB = []byte{0, 0, 0}
