package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"image/color"
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
	for i := uint(0); i < 512; i++ {
		ch := the_ram.Peek1(base + i)
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

var defaultCoco3Palette = []color.RGBA{
	{R: 50, G: 200, B: 50, A: 255},   // 0: Green
	{R: 220, G: 220, B: 50, A: 255},  // 1: Yellow
	{R: 50, G: 80, B: 220, A: 255},   // 2: Blue
	{R: 220, G: 50, B: 50, A: 255},   // 3: Red
	{R: 240, G: 240, B: 240, A: 255}, // 4: White
	{R: 50, G: 200, B: 200, A: 255},  // 5: Cyan
	{R: 200, G: 50, B: 200, A: 255},  // 6: Magenta
	{R: 240, G: 140, B: 40, A: 255},  // 7: Orange
	{R: 0, G: 0, B: 0, A: 255},       // 8: Black
	{R: 50, G: 200, B: 50, A: 255},   // 9: Green
	{R: 0, G: 0, B: 0, A: 255},       // 10: Black
	{R: 240, G: 240, B: 240, A: 255}, // 11: White
	{R: 0, G: 0, B: 0, A: 255},       // 12: Black
	{R: 50, G: 200, B: 50, A: 255},   // 13: Green
	{R: 0, G: 0, B: 0, A: 255},       // 14: Black
	{R: 240, G: 140, B: 40, A: 255},  // 15: Orange
}

func IsGimeGraphics() bool {
	if the_ram == nil {
		return false
	}
	// GIME VMODE register is at $FF98. Bit 7 is 1 for Graphics mode, 0 for Text mode.
	return (the_ram.Peek1(0xFF98) & 0x80) != 0
}

func GetHscreenScreen() []byte {
	vres := the_ram.Peek1(0xFF99)
	cres := vres & 0x03        // Bits 0-1: Color resolution (0=2 colors, 1=4 colors, 2=16 colors)
	hres := (vres >> 2) & 0x07 // Bits 2-4: Horizontal resolution (bytes per row)
	lpf := (vres >> 5) & 0x03  // Bits 5-6: Lines per field (0=192, 1=200, 2=210, 3=225)

	var height int
	switch lpf {
	case 1:
		height = 200
	case 2:
		height = 210
	case 3:
		height = 225
	default:
		height = 192
	}

	var bpp int
	switch cres {
	case 2: // 16 colors (4 bpp)
		bpp = 4
	case 1: // 4 colors (2 bpp)
		bpp = 2
	case 0: // 2 colors (1 bpp)
		bpp = 1
	default:
		bpp = 4
	}

	var bytesPerRow int
	switch hres {
	case 0:
		bytesPerRow = 16
	case 1:
		bytesPerRow = 20
	case 2:
		bytesPerRow = 32
	case 3:
		bytesPerRow = 40
	case 4:
		bytesPerRow = 64
	case 5:
		bytesPerRow = 80
	case 6:
		bytesPerRow = 128
	case 7:
		bytesPerRow = 160
	default:
		bytesPerRow = 160
	}

	width := bytesPerRow * (8 / bpp)

	palette := make([][]byte, 16)
	hasNonZero := false
	for i := uint(0); i < 16; i++ {
		code := the_ram.Peek1(0xFFB0 + i) & 0x3F
		if code != 0 {
			hasNonZero = true
		}
		c := coco3ColorToRGBA(code)
		palette[i] = []byte{c.R, c.G, c.B}
	}
	if !hasNonZero {
		for i := 0; i < 16; i++ {
			c := defaultCoco3Palette[i]
			palette[i] = []byte{c.R, c.G, c.B}
		}
	}

	offset1 := uint(the_ram.Peek1(0xFF9D))
	offset0 := uint(the_ram.Peek1(0xFF9E))
	base := ((offset1<<8 | offset0) * 8) & COCO3_RAM_MASK

	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(0)) // X
	binary.Write(&buf, binary.LittleEndian, uint16(0)) // Y

	if width == 640 {
		// Downsample 2:1 horizontally to 320x192
		binary.Write(&buf, binary.LittleEndian, uint16(320))
		binary.Write(&buf, binary.LittleEndian, uint16(uint16(height)))

		for y := 0; y < height; y++ {
			rowOffset := base + uint(y*bytesPerRow)
			for byteCol := 0; byteCol < bytesPerRow; byteCol += 2 {
				if bpp == 1 {
					b0 := the_ram.PPeek1(rowOffset + uint(byteCol))
					b1 := byte(0)
					if byteCol+1 < bytesPerRow {
						b1 = the_ram.PPeek1(rowOffset + uint(byteCol+1))
					}
					for bit := 0; bit < 8; bit += 2 {
						colIdx := (b0 >> (7 - bit)) & 1
						buf.Write(palette[colIdx])
					}
					for bit := 0; bit < 8; bit += 2 {
						colIdx := (b1 >> (7 - bit)) & 1
						buf.Write(palette[colIdx])
					}
				} else if bpp == 2 {
					b0 := the_ram.PPeek1(rowOffset + uint(byteCol))
					c0 := (b0 >> 6) & 3
					c2 := (b0 >> 2) & 3
					buf.Write(palette[c0])
					buf.Write(palette[c2])
					if byteCol+1 < bytesPerRow {
						b1 := the_ram.PPeek1(rowOffset + uint(byteCol+1))
						c0_1 := (b1 >> 6) & 3
						c2_1 := (b1 >> 2) & 3
						buf.Write(palette[c0_1])
						buf.Write(palette[c2_1])
					}
				}
			}
		}
	} else {
		// width == 320
		binary.Write(&buf, binary.LittleEndian, uint16(320))
		binary.Write(&buf, binary.LittleEndian, uint16(uint16(height)))

		for y := 0; y < height; y++ {
			rowOffset := base + uint(y*bytesPerRow)
			for byteCol := 0; byteCol < bytesPerRow; byteCol++ {
				b := the_ram.PPeek1(rowOffset + uint(byteCol))
				switch bpp {
				case 4: // 2 pixels per byte (16 colors)
					p0 := (b >> 4) & 0x0F
					p1 := b & 0x0F
					buf.Write(palette[p0])
					buf.Write(palette[p1])
				case 2: // 4 pixels per byte (4 colors)
					for p := 0; p < 4; p++ {
						colIdx := (b >> (6 - p*2)) & 0x03
						buf.Write(palette[colIdx])
					}
				case 1: // 8 pixels per byte (2 colors)
					for bit := 0; bit < 8; bit++ {
						colIdx := (b >> (7 - bit)) & 1
						buf.Write(palette[colIdx])
					}
				}
			}
		}
	}

	return buf.Bytes()
}

func GetScreenForWebsocket() []byte {
	if the_ram == nil {
		return nil
	}
	if IsGimeGraphics() {
		return GetHscreenScreen()
	}

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
		for x := uint(0); x < 256/8; x++ {
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
