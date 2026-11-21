package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"image/color"
	"os"
	"strings"
	"sync"
	"time"
)

var VDG_TEXT = flag.Bool("vdg_text", false, "whether to show VDG text")

var SamBits uint

var liveCoco2Mu sync.Mutex
var liveCoco2Active bool
var livePia1OutB byte
var liveCoco2Unsupported bool

func SamModeV() uint         { return 7 & SamBits }
func SamModeF() uint         { return 127 & (SamBits >> 3) }
func SamModeP() uint         { return 1 & (SamBits >> 10) }
func SamModeR() uint         { return 3 & (SamBits >> 11) }
func SamModeM() uint         { return 3 & (SamBits >> 13) }
func SamModeTY() uint        { return 1 & (SamBits >> 15) }
func SamScreenAddress() uint { return SamModeF() << 9 }

func Pia1OutB() byte {
	liveCoco2Mu.Lock()
	active := liveCoco2Active
	p1b := livePia1OutB
	liveCoco2Mu.Unlock()
	if active {
		return p1b
	}
	if the_ram == nil {
		return 0
	}
	return the_ram.Peek1(0xFF22)
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

// coco2VideoPhysAddr returns the physical RAM address scanned by SAM/VDG or GIME in CoCo-compatible
// mode (COCO=1). On 128K CoCo 3 BASIC, GIME always scans compatibility modes from physical Bank $38..$3B
// ($10000..$17FFF), independent of the CPU's active MMU task registers ($FFA0..$FFAF).
func coco2VideoPhysAddr(addr uint) uint {
	if the_ram == nil {
		return addr & 0xFFFF
	}
	if c3, ok := the_ram.(*Coco3Ram); ok && !c3.mmu256K && (*BOOTMODE&1024) == 0 && *LEVEL == 0 {
		return (0x10000 + (addr & 0x7FFF)) & c3.RamMask()
	}
	if (*BOOTMODE & 1024) != 0 {
		return addr & the_ram.RamMask()
	}
	return the_ram.Physical(addr & 0xFFFF)
}

func coco2VideoPeek1(addr uint) byte {
	if the_ram == nil {
		return 0
	}
	if c3, ok := the_ram.(*Coco3Ram); ok && !c3.mmu256K && (*BOOTMODE&1024) == 0 && *LEVEL == 0 {
		return c3.PPeek1(0x10000 + (addr & 0x7FFF))
	}
	if (*BOOTMODE & 1024) != 0 {
		return the_ram.PPeek1(addr)
	}
	return the_ram.Peek1(addr & 0xFFFF)
}

// coco3CompatPalette returns the 16 GIME palette RGB slices ($FFB0..$FFBF) when running on Coco3Ram
// and the palette registers have been initialized by ROM/software (at least two distinct entries).
func coco3CompatPalette() ([16][]byte, bool) {
	var pal [16][]byte
	c3, ok := the_ram.(*Coco3Ram)
	if !ok {
		return pal, false
	}
	io := c3.IoPhys()
	first := c3.PPeek1(io+0xB0) & 0x3F
	distinct := false
	for i := uint(0); i < 16; i++ {
		code := c3.PPeek1(io+0xB0+i) & 0x3F
		if code != first {
			distinct = true
		}
		rgba := coco3ColorToRGBA(code)
		pal[i] = []byte{rgba.R, rgba.G, rgba.B}
	}
	return pal, distinct
}

func ScanTextContents() []byte {
	if SamModeV() != 0 {
		return nil
	}

	const base = 0x0400
	z := make([]byte, 512)
	for i := uint(0); i < 512; i++ {
		ch := coco2VideoPeek1(base + i)
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
	if _, isCoco3 := the_ram.(*Coco3Ram); !isCoco3 {
		return false
	}
	init0 := the_ram.Peek1(0xFF90)
	if (init0 & 0x80) != 0 {
		return false // CoCo compatible mode (SAM/VDG)
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
	base := ((offset1<<8 | offset0) * 8) & the_ram.RamMask()

	renderHeight := height
	if renderHeight > 200 {
		renderHeight = 200
	}
	SyncLiveScreenRam(base, renderHeight*bytesPerRow)

	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(0)) // X
	binary.Write(&buf, binary.LittleEndian, uint16(0)) // Y

	if width == 640 {
		// Downsample 2:1 horizontally to 320xrenderHeight
		binary.Write(&buf, binary.LittleEndian, uint16(320))
		binary.Write(&buf, binary.LittleEndian, uint16(uint16(renderHeight)))

		for y := 0; y < renderHeight; y++ {
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
						if colIdx == 0 {
							colIdx = (b0 >> (6 - bit)) & 1
						}
						buf.Write(palette[colIdx])
					}
					for bit := 0; bit < 8; bit += 2 {
						colIdx := (b1 >> (7 - bit)) & 1
						if colIdx == 0 {
							colIdx = (b1 >> (6 - bit)) & 1
						}
						buf.Write(palette[colIdx])
					}
				} else if bpp == 2 {
					b0 := the_ram.PPeek1(rowOffset + uint(byteCol))
					c0 := (b0 >> 6) & 3
					if c0 == 0 {
						c0 = (b0 >> 4) & 3
					}
					c2 := (b0 >> 2) & 3
					if c2 == 0 {
						c2 = b0 & 3
					}
					buf.Write(palette[c0])
					buf.Write(palette[c2])
					if byteCol+1 < bytesPerRow {
						b1 := the_ram.PPeek1(rowOffset + uint(byteCol+1))
						c0_1 := (b1 >> 6) & 3
						if c0_1 == 0 {
							c0_1 = (b1 >> 4) & 3
						}
						c2_1 := (b1 >> 2) & 3
						if c2_1 == 0 {
							c2_1 = b1 & 3
						}
						buf.Write(palette[c0_1])
						buf.Write(palette[c2_1])
					}
				}
			}
		}
	} else {
		// width <= 320
		binary.Write(&buf, binary.LittleEndian, uint16(320))
		binary.Write(&buf, binary.LittleEndian, uint16(uint16(renderHeight)))

		for y := 0; y < renderHeight; y++ {
			rowOffset := base + uint(y*bytesPerRow)
			pixelsWritten := 0
			for byteCol := 0; byteCol < bytesPerRow; byteCol++ {
				b := the_ram.PPeek1(rowOffset + uint(byteCol))
				switch bpp {
				case 4: // 2 pixels per byte (16 colors)
					p0 := (b >> 4) & 0x0F
					p1 := b & 0x0F
					buf.Write(palette[p0])
					buf.Write(palette[p1])
					pixelsWritten += 2
				case 2: // 4 pixels per byte (4 colors)
					for p := 0; p < 4; p++ {
						colIdx := (b >> (6 - p*2)) & 0x03
						buf.Write(palette[colIdx])
					}
					pixelsWritten += 4
				case 1: // 8 pixels per byte (2 colors)
					for bit := 0; bit < 8; bit++ {
						colIdx := (b >> (7 - bit)) & 1
						buf.Write(palette[colIdx])
					}
					pixelsWritten += 8
				}
			}
			for p := pixelsWritten; p < 320; p++ {
				buf.Write(palette[0])
			}
		}
	}

	return buf.Bytes()
}

func IsGimeText() bool {
	if the_ram == nil {
		return false
	}
	if _, isCoco3 := the_ram.(*Coco3Ram); !isCoco3 {
		return false
	}
	init0 := the_ram.Peek1(0xFF90)
	if (init0 & 0x80) != 0 {
		return false // CoCo compatible mode (SAM/VDG)
	}
	if (init0 & 0x40) == 0 {
		return false // GIME native modes require MMU enabled (and init0 configured)
	}
	// GIME Non-CoCo mode (bit 7 of $FF90 is 0) and Text mode (bit 7 of $FF98 is 0)
	return (the_ram.Peek1(0xFF98) & 0x80) == 0
}

func GetGimeTextScreen() []byte {
	vres := the_ram.Peek1(0xFF99)
	hres := (vres >> 2) & 0x07
	hasAttrs := (vres & 1) != 0

	cols := 40
	if hres == 5 || hres == 7 {
		cols = 80
	} else if hres == 0 || hres == 2 {
		cols = 32
	} else if hres == 4 || hres == 6 {
		cols = 64
	}

	const rows = 24
	const height = 192

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
	base := ((offset1<<8 | offset0) * 8) & the_ram.RamMask()
	hoff := the_ram.Peek1(0xFF9F)

	stride := uint(1)
	if hasAttrs {
		stride = 2
	}
	rowStride := uint(cols) * stride
	if (hoff & 0x80) != 0 {
		rowStride = 256
	}
	horizOff := uint(hoff&0x7F) * 2

	SyncLiveScreenRam(base, int(rowStride)*rows)

	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(0))              // X
	binary.Write(&buf, binary.LittleEndian, uint16(0))              // Y
	binary.Write(&buf, binary.LittleEndian, uint16(320))            // W (canvas width)
	binary.Write(&buf, binary.LittleEndian, uint16(uint16(height))) // H

	sampleCols := [4]int{1, 3, 5, 0}

	for row := 0; row < rows; row++ {
		rowBase := base + uint(row)*rowStride + horizOff
		for py := 0; py < 8; py++ {
			for col := 0; col < cols; col++ {
				charOffset := rowBase + uint(col)*stride
				rawCh := the_ram.PPeek1(charOffset)
				glyphCh := rawCh & 0x7F
				if glyphCh == 0 {
					glyphCh = ' '
				} else if glyphCh < 32 {
					// GIME character ROM maps 0x01..0x1F to lowercase/punctuation 0x61..0x7F
					glyphCh += 0x60
				}
				var fg, bg []byte
				underline := false
				if hasAttrs {
					attr := the_ram.PPeek1(charOffset + 1)
					fg = palette[8+((attr>>3)&7)]
					bg = palette[attr&7]
					underline = (attr&0x40) != 0 && py == 7
				} else {
					fg = palette[8] // default black
					bg = palette[0] // default green
				}

				if cols == 80 {
					// 80 columns: 4 pixels per character cell, downsampling 2:1 horizontally
					for px := 0; px < 4; px++ {
						if underline || getGlyphPixel(glyphCh, sampleCols[px], py) {
							buf.Write(fg)
						} else {
							buf.Write(bg)
						}
					}
				} else {
					// 40 columns (or <= 40): 8 pixels per character cell
					for px := 0; px < 8; px++ {
						if underline || getGlyphPixel(glyphCh, px, py) {
							buf.Write(fg)
						} else {
							buf.Write(bg)
						}
					}
				}
			}
			// Pad remaining scanline pixels if cols < 40
			for p := cols * 8; p < 320; p++ {
				buf.Write(palette[0])
			}
		}
	}

	return buf.Bytes()
}

var lastLiveScreenSync time.Time
var liveScreenSyncBusy bool
var liveScreenSyncMu sync.Mutex

func SyncLiveScreenRam(addr uint, size int) {
	liveScreenSyncMu.Lock()
	if liveScreenSyncBusy {
		liveScreenSyncMu.Unlock()
		return
	}
	if time.Since(lastLiveScreenSync) < 80*time.Millisecond {
		liveScreenSyncMu.Unlock()
		return
	}
	if !LastWriteCycleTime.IsZero() && time.Since(LastWriteCycleTime) < 300*time.Millisecond {
		liveScreenSyncMu.Unlock()
		return
	}
	ch := GetChannelToPico()
	if ch == nil {
		liveScreenSyncMu.Unlock()
		return
	}
	liveScreenSyncBusy = true
	lastLiveScreenSync = time.Now()
	liveScreenSyncMu.Unlock()

	go func() {
		defer func() {
			liveScreenSyncMu.Lock()
			liveScreenSyncBusy = false
			liveScreenSyncMu.Unlock()
		}()

		_, isCoco3 := the_ram.(*Coco3Ram)
		liveCoco2Mu.Lock()
		unsupported := liveCoco2Unsupported
		liveCoco2Mu.Unlock()

		if !unsupported && !isCoco3 {
			resp, err := PicoRpcCallReq(ch, RpcRequest{
				Method: "get-coco2-video",
				Offset: 0,
				Length: 1024,
			}, 1500*time.Millisecond)
			if err == nil && resp.Status == 0 {
				sam := uint(resp.Handle) & 0xFFFF
				p1b := byte((uint(resp.Handle) >> 16) & 0xFF)
				total := resp.Size
				data := append([]byte(nil), resp.Data...)
				ok := true
				for len(data) < total {
					r2, err2 := PicoRpcCallReq(ch, RpcRequest{
						Method: "get-coco2-video",
						Offset: len(data),
						Length: 1024,
					}, 1500*time.Millisecond)
					if err2 != nil || r2.Status != 0 || len(r2.Data) == 0 {
						ok = false
						break
					}
					data = append(data, r2.Data...)
				}
				if ok {
					SamBits = sam
					liveCoco2Mu.Lock()
					liveCoco2Active = true
					livePia1OutB = p1b
					liveCoco2Mu.Unlock()
					fb := (sam >> 3 & 0x7F) << 9
					if the_ram != nil {
						raw := the_ram.GetTrackRam()
						if raw != nil && int(fb)+len(data) <= len(raw) {
							copy(raw[fb:], data)
						}
					}
					return
				}
			} else if err == nil && resp.Status != 0 {
				liveCoco2Mu.Lock()
				liveCoco2Active = false
				if strings.Contains(strings.ToLower(resp.Message), "unknown") {
					liveCoco2Unsupported = true
				}
				liveCoco2Mu.Unlock()
			}
		}

		data, err := fetchRamRange(ch, int(addr), size)
		if err == nil && len(data) == size && the_ram != nil {
			raw := the_ram.GetTrackRam()
			if raw != nil && int(addr)+size <= len(raw) {
				copy(raw[addr:], data)
			}
		}
	}()
}

// Coco2ActiveVideoMode returns (-1, 512) for 32x16 Text/SG4 or (gm 0..7, fbBytes) for MC6847 graphics modes.
func Coco2ActiveVideoMode() (gm int, fbBytes int) {
	p1b := Pia1OutB()
	if (p1b & 0x80) != 0 {
		gm = int((p1b >> 4) & 7)
	} else {
		liveCoco2Mu.Lock()
		active := liveCoco2Active
		liveCoco2Mu.Unlock()
		if !active && SamModeV() != 0 {
			switch SamModeV() {
			case 1:
				gm = 0
			case 2:
				gm = 2
			case 3:
				gm = 3
			case 4:
				gm = 4
			case 5:
				gm = 5
			default:
				gm = 7
			}
		} else {
			return -1, 512
		}
	}
	switch gm {
	case 0, 1:
		return gm, 1024
	case 2:
		return gm, 2048
	case 3:
		return gm, 1536
	case 4, 5:
		return gm, 3072
	default:
		return gm, 6144
	}
}

func Coco2StatusLine() string {
	if IsGimeGraphics() {
		vres := the_ram.Peek1(0xFF99)
		cres := vres & 0x03
		hres := (vres >> 2) & 0x07
		bpp := [4]int{1, 2, 4, 4}[cres]
		bpr := [8]int{16, 20, 32, 40, 64, 80, 128, 160}[hres]
		w := bpr * (8 / bpp)
		ncolors := 1 << bpp
		base := ((uint(the_ram.Peek1(0xFF9D))<<8 | uint(the_ram.Peek1(0xFF9E))) * 8) & the_ram.RamMask()
		return fmt.Sprintf("GIME GFX %dx192x%d @$%05X", w, ncolors, base)
	}
	if IsGimeText() {
		vres := the_ram.Peek1(0xFF99)
		hres := (vres >> 2) & 0x07
		cols := 40
		if hres == 5 || hres == 7 {
			cols = 80
		} else if hres == 0 || hres == 2 {
			cols = 32
		} else if hres == 4 || hres == 6 {
			cols = 64
		}
		base := ((uint(the_ram.Peek1(0xFF9D))<<8 | uint(the_ram.Peek1(0xFF9E))) * 8) & the_ram.RamMask()
		return fmt.Sprintf("GIME TEXT %dx24 @$%05X", cols, base)
	}
	fb := SamScreenAddress()
	if fb == 0 {
		fb = 0x0400
	}
	p1b := Pia1OutB()
	css := (p1b >> 3) & 1
	gm, _ := Coco2ActiveVideoMode()
	_, isCoco3 := the_ram.(*Coco3Ram)
	if gm < 0 {
		if isCoco3 {
			return fmt.Sprintf("GIME VDG TEXT/SG4 @$%05X CSS%d", coco2VideoPhysAddr(fb), css)
		}
		return fmt.Sprintf("VDG TEXT/SG4  @$%04X  CSS%d", fb, css)
	}
	names := [8]string{"CG1 64x64x4", "RG1 128x64x2", "CG2 128x64x4", "PMODE0 128x96x2", "PMODE1 128x96x4", "PMODE2 128x192x2", "PMODE3 128x192x4", "PMODE4 256x192x2"}
	if isCoco3 {
		return fmt.Sprintf("GIME %s @$%05X", names[gm&7], coco2VideoPhysAddr(fb))
	}
	return fmt.Sprintf("VDG %s @$%04X CSS%d", names[gm&7], fb, css)
}

func GetScreenForWebsocket() []byte {
	if the_ram == nil {
		return nil
	}
	if IsGimeGraphics() {
		return GetHscreenScreen()
	}
	if IsGimeText() {
		return GetGimeTextScreen()
	}

	fb := SamScreenAddress()
	if fb == 0 {
		fb = 0x0400
	}
	gm, fbBytes := Coco2ActiveVideoMode()
	p1b := Pia1OutB()
	if *RAM_VERBOSE {
		Logf("SAM V=%d gm=%d addr=$%04x P1B=$%02x", SamModeV(), gm, fb, p1b)
	}

	SyncLiveScreenRam(coco2VideoPhysAddr(fb), fbBytes)
	if gm < 0 {
		return GetCoco2TextScreen(fb, p1b)
	}
	return GetCoco2GraphicsScreen(fb, gm, p1b)
}

// GetCoco2TextScreen renders the 32x16 MC6847 Alphanumeric + Semigraphics-4 screen as one 256x192 OpBitmap at (32, 4).
func GetCoco2TextScreen(base uint, p1b byte) []byte {
	const outW = 256
	const outH = 192
	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(32))
	binary.Write(&buf, binary.LittleEndian, uint16(4))
	binary.Write(&buf, binary.LittleEndian, uint16(outW))
	binary.Write(&buf, binary.LittleEndian, uint16(outH))
	buf.Grow(outW * outH * 3)

	gimePal, useGimePal := coco3CompatPalette()
	textDark := BlackRGB
	textBright := VdgSemiGraphicsColors[0] // green (#32C832)
	if (p1b & 0x08) != 0 {
		textBright = VdgSemiGraphicsColors[7] // orange CSS=1
	}
	if useGimePal {
		if (p1b & 0x08) != 0 {
			textDark = gimePal[14]
			textBright = gimePal[15]
		} else {
			textDark = gimePal[12]
			textBright = gimePal[13]
		}
	}

	for row := uint(0); row < 16; row++ {
		for cy := uint(0); cy < 12; cy++ {
			for col := uint(0); col < 32; col++ {
				addr := (base + row*32 + col) & 0xFFFF
				ch := coco2VideoPeek1(addr)
				if (ch & 0x80) == 0 {
					// Alphanumeric: bit 6 = 1 ($40..$7F) is normal dark-on-bright; bit 6 = 0 ($00..$3F) is inverted bright-on-dark.
					normalBg := (ch & 0x40) != 0
					var glyphRow byte
					if cy >= 3 && cy < 10 {
						glyphRow = VdgFont[7*uint(ch&0x3F)+(cy-3)]
					}
					for cx := uint(0); cx < 8; cx++ {
						stroke := cy >= 3 && cy < 10 && ((glyphRow>>(7-cx))&1) != 0
						if normalBg {
							if stroke {
								buf.Write(textDark)
							} else {
								buf.Write(textBright)
							}
						} else {
							if stroke {
								buf.Write(textBright)
							} else {
								buf.Write(textDark)
							}
						}
					}
				} else {
					// Semigraphics-4: bits 6..4 select color (0..7), bits 3..0 select TL, TR, BL, BR quadrants.
					rgb := VdgSemiGraphicsColors[(ch>>4)&7]
					sgOff := BlackRGB
					if useGimePal {
						rgb = gimePal[(ch>>4)&7]
					}
					leftOn := (ch & 0x08) != 0
					rightOn := (ch & 0x04) != 0
					if cy >= 6 {
						leftOn = (ch & 0x02) != 0
						rightOn = (ch & 0x01) != 0
					}
					for cx := uint(0); cx < 4; cx++ {
						if leftOn {
							buf.Write(rgb)
						} else {
							buf.Write(sgOff)
						}
					}
					for cx := uint(0); cx < 4; cx++ {
						if rightOn {
							buf.Write(rgb)
						} else {
							buf.Write(sgOff)
						}
					}
				}
			}
		}
	}
	return buf.Bytes()
}

// GetCoco2GraphicsScreen renders MC6847 full-graphics modes GM=0..7 (incl. PMODE 0..4) scaled to 256x192 at (32, 4).
func GetCoco2GraphicsScreen(base uint, gm int, p1b byte) []byte {
	var srcW, srcH, bpp uint
	switch gm & 7 {
	case 0: // CG1: 64x64x4
		srcW, srcH, bpp = 64, 64, 2
	case 1: // RG1: 128x64x2
		srcW, srcH, bpp = 128, 64, 1
	case 2: // CG2: 128x64x4
		srcW, srcH, bpp = 128, 64, 2
	case 3: // RG2 / PMODE 0: 128x96x2
		srcW, srcH, bpp = 128, 96, 1
	case 4: // CG3 / PMODE 1: 128x96x4
		srcW, srcH, bpp = 128, 96, 2
	case 5: // RG3 / PMODE 2: 128x192x2
		srcW, srcH, bpp = 128, 192, 1
	case 6: // CG6 / PMODE 3: 128x192x4
		srcW, srcH, bpp = 128, 192, 2
	default: // RG6 / PMODE 4: 256x192x2
		srcW, srcH, bpp = 256, 192, 1
	}

	const outW = 256
	const outH = 192
	scaleX := outW / srcW
	scaleY := outH / srcH
	bytesPerRow := srcW * bpp / 8
	colorBias := uint((p1b & 0x08) >> 1) // 0 (Green/Yellow/Blue/Red) or 4 (Buff/Cyan/Magenta/Orange)

	var buf bytes.Buffer
	buf.WriteByte(OpBitmap)
	binary.Write(&buf, binary.LittleEndian, uint16(32))
	binary.Write(&buf, binary.LittleEndian, uint16(4))
	binary.Write(&buf, binary.LittleEndian, uint16(outW))
	binary.Write(&buf, binary.LittleEndian, uint16(outH))
	buf.Grow(outW * outH * 3)

	gimePal, useGimePal := coco3CompatPalette()
	bg1 := BlackRGB
	fg1 := VdgSemiGraphicsColors[colorBias] // Green (CSS=0) or Buff (CSS=1)
	if useGimePal {
		if (p1b & 0x08) != 0 {
			bg1 = gimePal[10]
			fg1 = gimePal[11]
		} else {
			bg1 = gimePal[8]
			fg1 = gimePal[9]
		}
	}
	for sy := uint(0); sy < srcH; sy++ {
		rowAddr := base + sy*bytesPerRow
		for ry := uint(0); ry < scaleY; ry++ {
			for bx := uint(0); bx < bytesPerRow; bx++ {
				b := coco2VideoPeek1((rowAddr + bx) & 0xFFFF)
				if bpp == 1 {
					for bit := uint(0); bit < 8; bit++ {
						pix := bg1
						if ((b >> (7 - bit)) & 1) != 0 {
							pix = fg1
						}
						for rx := uint(0); rx < scaleX; rx++ {
							buf.Write(pix)
						}
					}
				} else {
					for p := uint(0); p < 4; p++ {
						cidx := colorBias + uint((b>>(6-2*p))&3)
						pix := VdgSemiGraphicsColors[cidx]
						if useGimePal {
							pix = gimePal[cidx]
						}
						for rx := uint(0); rx < scaleX; rx++ {
							buf.Write(pix)
						}
					}
				}
			}
		}
	}
	return buf.Bytes()
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
			var ch byte
			if (*BOOTMODE & 1024) != 0 {
				ch = the_ram.PPeek1(p)
			} else {
				ch = the_ram.Peek1(p)
			}
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
