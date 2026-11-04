package main

import (
	"bytes"
	"encoding/binary"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestServeRam(t *testing.T) {
	// Initialize dummy 64KB RAM
	ram := new(Coco1Ram)
	the_ram = ram

	// Put test pattern at 0x0400 (screen RAM)
	// 0x0F ("O"), 0x0B ("K"), 0x00 ("@"), 0x20 (" "), 0x01 ("A")
	ram.Poke1(0x0400, 0x0F) // VDG "O" (code 15), ASCII non-printable
	ram.Poke1(0x0401, 0x0B) // VDG "K" (code 11)
	ram.Poke1(0x0402, 0x00) // VDG "@" (code 0)
	ram.Poke1(0x0403, 0x20) // VDG " " (code 32)
	ram.Poke1(0x0404, 0x01) // VDG "A" (code 1)
	ram.Poke1(0x0405, 0x41) // ASCII "A" (0x41), VDG inverted "A" (code 65 -> 1 -> "A")

	// 1. Test default format (hexdump with dual ASCII + VDG views)
	req := httptest.NewRequest("GET", "/ram?addr=0x0400&len=16", nil)
	w := httptest.NewRecorder()
	serveRam(w, req)

	resp := w.Result()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("expected 200, got %d", resp.StatusCode)
	}
	body := w.Body.String()
	t.Logf("Hexdump output:\n%s", body)

	if !strings.HasPrefix(body, "00000400  0f 0b 00 20 01 41") {
		t.Errorf("unexpected hexdump prefix: %q", body)
	}
	// Check standard ASCII column (|... .A..........|) and VDG column (|OK@ A...........|)
	if !strings.Contains(body, "|OK@ A") {
		t.Errorf("VDG column missing expected chars: %q", body)
	}

	// 2. Test format=bin
	reqBin := httptest.NewRequest("GET", "/ram?addr=0x0400&len=6&format=bin", nil)
	wBin := httptest.NewRecorder()
	serveRam(wBin, reqBin)
	if wBin.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", wBin.Code)
	}
	if wBin.Header().Get("Content-Type") != "application/octet-stream" {
		t.Errorf("expected application/octet-stream, got %s", wBin.Header().Get("Content-Type"))
	}
	expectedBytes := []byte{0x0F, 0x0B, 0x00, 0x20, 0x01, 0x41}
	if wBin.Body.String() != string(expectedBytes) {
		t.Errorf("unexpected binary content: %x", wBin.Body.Bytes())
	}

	// 3. Test format=hex
	reqHex := httptest.NewRequest("GET", "/ram?addr=0x0400&len=6&format=hex", nil)
	wHex := httptest.NewRecorder()
	serveRam(wHex, reqHex)
	if wHex.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", wHex.Code)
	}
	if wHex.Body.String() != "0f0b00200141\n" {
		t.Errorf("unexpected hex output: %q", wHex.Body.String())
	}

	// 4. Test /ram.hex path
	reqPathHex := httptest.NewRequest("GET", "/ram.hex?addr=0x0400&len=6", nil)
	wPathHex := httptest.NewRecorder()
	serveRam(wPathHex, reqPathHex)
	if wPathHex.Body.String() != "0f0b00200141\n" {
		t.Errorf("unexpected /ram.hex output: %q", wPathHex.Body.String())
	}
}

func TestGetHscreenModes(t *testing.T) {
	ram := new(Coco3Ram)
	the_ram = ram

	// Enable GIME Graphics ($FF98 bit 7 = 1)
	ram.Poke1(0xFF98, 0x80)
	if !IsGimeGraphics() {
		t.Fatalf("expected IsGimeGraphics to be true")
	}

	// Test all 4 standard CoCo 3 HSCREEN modes
	// Mode 1: 0x15 (320x192, 4 colors, bpp=2)
	// Mode 2: 0x1E (320x192, 16 colors, bpp=4)
	// Mode 3: 0x14 (640x192, 2 colors, bpp=1, downsampled to 320)
	// Mode 4: 0x1D (640x192, 4 colors, bpp=2, downsampled to 320)
	testModes := []struct {
		mode    int
		ff99Val byte
	}{
		{1, 0x15},
		{2, 0x1E},
		{3, 0x14},
		{4, 0x1D},
	}

	for _, tm := range testModes {
		ram.Poke1(0xFF99, tm.ff99Val)
		ram.Poke1(0xFF9D, 0x00) // VSC MSB
		ram.Poke1(0xFF9E, 0x00) // VSC LSB

		screenData := GetHscreenScreen()
		if screenData == nil {
			t.Fatalf("HSCREEN %d: GetHscreenScreen returned nil", tm.mode)
		}

		// Expected length: 1 (OpBitmap) + 2 (X) + 2 (Y) + 2 (W) + 2 (H) + (320 * 192 * 3 RGB)
		expectedLen := 9 + (320 * 192 * 3)
		if len(screenData) != expectedLen {
			t.Errorf("HSCREEN %d: expected length %d, got %d", tm.mode, expectedLen, len(screenData))
		}
		if screenData[0] != OpBitmap {
			t.Errorf("HSCREEN %d: expected OpBitmap %d, got %d", tm.mode, OpBitmap, screenData[0])
		}
	}
}

func TestGetGimeTextModes(t *testing.T) {
	ram := new(Coco3Ram)
	the_ram = ram

	// 1. In standard CoCo 2/3 WIDTH 32 mode:
	// $FF90 has bit 7 = 1 (COCO compatible)
	ram.Poke1(0xFF90, 0xCC)
	ram.Poke1(0xFF98, 0x00)
	if IsGimeText() {
		t.Errorf("expected IsGimeText to be false for WIDTH 32")
	}

	// 2. Test WIDTH 40 mode:
	// $FF90 has bit 7 = 0 (MMUEN+MC3+MC2 = 0x4C)
	// $FF98 = 0x03 (Text mode, 8 scanlines/row)
	// $FF99 = 0x05 (40 cols, attributes enabled)
	// $FF9D = 0xD8, $FF9E = 0x00 (Video start = 0xD800 * 8 & 0x1FFFF = 0x0C000)
	ram.Poke1(0xFF90, 0x4C)
	ram.Poke1(0xFF98, 0x03)
	ram.Poke1(0xFF99, 0x05)
	ram.Poke1(0xFF9D, 0xD8)
	ram.Poke1(0xFF9E, 0x00)

	if !IsGimeText() {
		t.Fatalf("expected IsGimeText to be true for WIDTH 40")
	}

	// Put test string at 0x0C000 with green background (attr 0), black foreground
	testStr := "HELLO COCO3 WIDTH 40"
	base := uint(0x0C000)
	for i := 0; i < len(testStr); i++ {
		ram.trackRam[base+uint(i*2)] = testStr[i]
		ram.trackRam[base+uint(i*2+1)] = 0x00 // attr: fg=8 (black), bg=0 (green)
	}

	screen40 := GetScreenForWebsocket()
	if screen40 == nil {
		t.Fatalf("GetScreenForWebsocket returned nil for WIDTH 40")
	}
	// Canvas size prefix (5) + bitmap header (9) + RGB pixels.
	expectedLen := 5 + 9 + (320 * 192 * 3)
	if len(screen40) != expectedLen {
		t.Errorf("WIDTH 40: expected len %d, got %d", expectedLen, len(screen40))
	}

	// 3. Test WIDTH 80 mode:
	// $FF99 = 0x15 (80 cols, attributes enabled)
	ram.Poke1(0xFF99, 0x15)

	screen80 := GetScreenForWebsocket()
	if screen80 == nil {
		t.Fatalf("GetScreenForWebsocket returned nil for WIDTH 80")
	}
	expectedLen = 5 + 9 + (640 * 192 * 3)
	if len(screen80) != expectedLen {
		t.Errorf("WIDTH 80: expected len %d, got %d", expectedLen, len(screen80))
	}

	// 4. Test CoCo 1/2 mode (Coco1Ram):
	the_ram = new(Coco1Ram)
	if IsGimeText() {
		t.Errorf("expected IsGimeText to be false for Coco1Ram")
	}
	if IsGimeGraphics() {
		t.Errorf("expected IsGimeGraphics to be false for Coco1Ram")
	}

	// 5. Test GetScreenForWebsocket for CoCo 1/2 Text mode:
	the_ram.Poke1(0x0400, 'D')
	screenCoco2 := GetScreenForWebsocket()
	if screenCoco2 == nil {
		t.Fatalf("GetScreenForWebsocket returned nil for CoCo 2 text")
	}
}

func TestCoco3ColorToRGBA(t *testing.T) {
	saved := RgbMonitor.Load()
	defer RgbMonitor.Store(saved)

	RgbMonitor.Store(false)
	for _, tc := range []struct {
		code    byte
		r, g, b uint8
	}{{0, 0, 0, 0}, {63, 255, 255, 255}, {9, 106, 17, 181}, {18, 71, 145, 5}, {38, 250, 140, 0}} {
		c := coco3ColorToRGBA(tc.code)
		if c.R != tc.r || c.G != tc.g || c.B != tc.b {
			t.Errorf("composite code %d: got %d,%d,%d", tc.code, c.R, c.G, c.B)
		}
	}

	RgbMonitor.Store(true)
	for _, tc := range []struct {
		code    byte
		r, g, b uint8
	}{
		{0, 0, 0, 0}, {63, 255, 255, 255}, {7, 71, 71, 71}, {56, 166, 166, 166},
		{36, 255, 0, 0}, {18, 0, 255, 0}, {9, 0, 0, 255}, {32, 166, 0, 0}, {4, 71, 0, 0}, {38, 255, 71, 0},
	} {
		c := coco3ColorToRGBA(tc.code)
		if c.R != tc.r || c.G != tc.g || c.B != tc.b {
			t.Errorf("code %d: got %d,%d,%d want %d,%d,%d", tc.code, c.R, c.G, c.B, tc.r, tc.g, tc.b)
		}
	}
}

func TestMonitorState(t *testing.T) {
	saved, savedRGB := the_ram, RgbMonitor.Load()
	defer func() { the_ram = saved; RgbMonitor.Store(savedRGB) }()

	the_ram = new(Coco1Ram)
	RgbMonitor.Store(true)
	if got := monitorState(); got != 0 {
		t.Errorf("CoCo 1/2: got %d", got)
	}
	the_ram = new(Coco3Ram)
	if got := monitorState(); got != 2 {
		t.Errorf("RGB: got %d", got)
	}
	RgbMonitor.Store(false)
	if got := monitorState(); got != 1 {
		t.Errorf("CMP: got %d", got)
	}
}

// BASIC's FF22 value, as CSS 0, for each PMODE.
var pmodeTests = []struct {
	pmode, v     uint
	ff22         byte
	rows, bytes  uint
	pixelW       int
	bitsPerPixel uint
}{
	{0, 3, 0xB0, 96, 16, 2, 1},
	{1, 4, 0xC0, 96, 32, 2, 2},
	{2, 5, 0xD0, 192, 16, 2, 1},
	{3, 6, 0xE0, 192, 32, 2, 2},
	{4, 6, 0xF0, 192, 32, 1, 1},
}

func screenPixel(frame []byte, x, y int) []byte {
	i := 9 + 3*(y*256+x)
	return frame[i : i+3]
}

// Every PMODE and color set on a CoCo 1/2, with FF22 as BASIC sets it.
func TestCoco12Pmodes(t *testing.T) {
	saved, savedSam := the_ram, SamBits
	defer func() { the_ram, SamBits = saved, savedSam }()

	const base = 0x0E00
	const pattern = 0xE4 // 11 10 01 00
	for _, m := range pmodeTests {
		for css := uint(0); css < 2; css++ {
			the_ram = new(Coco1Ram)
			the_ram.Poke1(0xFF23, 0x37) // Bit 3 is set here, as BASIC leaves it.
			the_ram.Poke1(0xFF22, m.ff22|byte(css<<3))
			the_ram.Poke1(base, pattern)
			the_ram.Poke1(base+m.bytes, 0xFF) // The second row in memory.

			g := samGraphicsGeometry[m.v]
			if g.rows != m.rows || g.bytesPerRow != m.bytes {
				t.Fatalf("PMODE %d: geometry %v", m.pmode, g)
			}
			frame := GetVdgGraphicsScreen(base, g.rows, g.bytesPerRow)
			if want := 9 + 3*256*192; len(frame) != want {
				t.Fatalf("PMODE %d: length %d, want %d", m.pmode, len(frame), want)
			}

			var want [][]byte
			if m.bitsPerPixel == 2 {
				set := VdgSemiGraphicsColors[4*css : 4*css+4]
				want = [][]byte{set[3], set[2], set[1], set[0]}
			} else {
				off, on := VdgResolutionColors[2*css], VdgResolutionColors[2*css+1]
				want = [][]byte{on, on, on, off, off, on, off, off}
			}
			for i, c := range want {
				for w := 0; w < m.pixelW; w++ {
					if got := screenPixel(frame, i*m.pixelW+w, 0); !bytes.Equal(got, c) {
						t.Errorf("PMODE %d CSS %d pixel %d: got %v, want %v", m.pmode, css, i, got, c)
					}
				}
			}

			scale := int(192 / m.rows)
			last, next := screenPixel(frame, 2, scale-1), screenPixel(frame, 0, scale)
			if !bytes.Equal(last, want[1]) {
				t.Errorf("PMODE %d CSS %d: row not repeated", m.pmode, css)
			}
			top := VdgSemiGraphicsColors[4*css+3]
			if m.bitsPerPixel == 1 {
				top = VdgResolutionColors[2*css+1]
			}
			if !bytes.Equal(next, top) {
				t.Errorf("PMODE %d CSS %d: second memory row got %v, want %v", m.pmode, css, next, top)
			}
		}
	}
}

func TestCoco12IgnoresPaletteRegisters(t *testing.T) {
	saved := the_ram
	defer func() { the_ram = saved }()
	the_ram = new(Coco1Ram)
	the_ram.Poke1(0xFFB0, 0x07)
	the_ram.Poke1(0xFF22, 0xC0)
	if got := screenPixel(GetVdgGraphicsScreen(0x0E00, 96, 32), 0, 0); !bytes.Equal(got, VdgSemiGraphicsColors[0]) {
		t.Errorf("got %v", got)
	}
}

func TestCoco3VdgModesUsePaletteRegisters(t *testing.T) {
	saved, savedRGB := the_ram, RgbMonitor.Load()
	defer func() { the_ram = saved; RgbMonitor.Store(savedRGB) }()
	RgbMonitor.Store(true)
	const base = 0x0E00
	rgb := func(code byte) []byte {
		c := coco3ColorToRGBA(code)
		return []byte{c.R, c.G, c.B}
	}

	for _, m := range []struct {
		ff22 byte
		data byte
		rows uint
		reg  uint
	}{
		{0xC0, 0xFF, 96, 0xFFB3}, // PMODE 1 CSS 0, color 3
		{0xC8, 0x00, 96, 0xFFB4}, // PMODE 1 CSS 1, color 0
		{0xF0, 0x00, 192, 0xFFB8},
		{0xF0, 0xFF, 192, 0xFFB9},
		{0xF8, 0x00, 192, 0xFFBA},
		{0xF8, 0xFF, 192, 0xFFBB},
	} {
		ram := new(Coco3Ram)
		the_ram = ram
		for r := uint(0xFFB0); r < 0xFFC0; r++ {
			ram.Poke1(r, byte(r-0xFFB0+1))
		}
		ram.Poke1(0xFF22, m.ff22)
		ram.Poke1(base, m.data)
		want := rgb(byte(m.reg - 0xFFB0 + 1))
		if got := screenPixel(GetVdgGraphicsScreen(base, m.rows, 32), 0, 0); !bytes.Equal(got, want) {
			t.Errorf("FF22 %02x data %02x: got %v, want %v from %04x", m.ff22, m.data, got, want, m.reg)
		}
	}

	// A palette never seen still draws something.
	ram := new(Coco3Ram)
	the_ram = ram
	ram.Poke1(0xFF22, 0xC0)
	if got := screenPixel(GetVdgGraphicsScreen(base, 96, 32), 0, 0); !bytes.Equal(got, VdgSemiGraphicsColors[0]) {
		t.Errorf("empty palette: got %v", got)
	}
}

func TestGraphicsFrameSizesCanvas(t *testing.T) {
	saved := the_ram
	defer func() { the_ram = saved }()
	the_ram = new(Coco1Ram)

	frame := GetVdgGraphicsScreen(0x0E00, 96, 16)
	got := wholeScreen(frame)
	if w, h := binary.LittleEndian.Uint16(got[1:]), binary.LittleEndian.Uint16(got[3:]); got[0] != OpSize || w != 256 || h != 192 || !bytes.Equal(got[5:], frame) {
		t.Errorf("op %d canvas %d x %d", got[0], w, h)
	}

	text := GetTextScreen(0x0400)
	got = withCanvasSize(text, 256, 192)
	if w, h := binary.LittleEndian.Uint16(got[1:]), binary.LittleEndian.Uint16(got[3:]); got[0] != OpSize || w != 256 || h != 192 || !bytes.Equal(got[5:], text) {
		t.Errorf("text: op %d canvas %d x %d", got[0], w, h)
	}
}

// Galactus maps $0000-$7FFF to blocks $38-$3B and its screen, $8000-$FFFF, to blocks $08-$0B.
func TestCoco3RamSizeSeparatesBlocks(t *testing.T) {
	saved, savedKb := the_ram, *COCO3_RAM_KB
	defer func() { the_ram = saved; *COCO3_RAM_KB = savedKb }()

	const lowByte, screenByte = 0x11, 0x22
	screenAfterLowWrite := func(kb uint) byte {
		*COCO3_RAM_KB = kb
		ram := new(Coco3Ram)
		the_ram = ram
		ram.Poke1(0xFF90, 0x40)
		ram.Poke1(0xFFA3, 0x3B)
		ram.Poke1(0xFFA7, 0x0B)
		ram.Poke1(0xF760, screenByte)
		ram.Poke1(0x7760, lowByte)
		return ram.PPeek1(0x17760)
	}

	if got := screenAfterLowWrite(512); got != screenByte {
		t.Errorf("512K: screen byte got %02x", got)
	}
	if got := screenAfterLowWrite(128); got != lowByte {
		t.Errorf("128K: blocks should alias, got %02x", got)
	}
}

func TestHorizontalOffsetRegister(t *testing.T) {
	saved, savedRGB := the_ram, RgbMonitor.Load()
	defer func() { the_ram = saved; RgbMonitor.Store(savedRGB) }()
	RgbMonitor.Store(true)

	ram := new(Coco3Ram)
	the_ram = ram
	ram.Poke1(0xFF99, 0x1E) // 320 x 192, 16 colors, 160 bytes per row
	ram.Poke1(0xFFB0, 0x00)
	ram.Poke1(0xFFB1, 0x3F)
	white := []byte{255, 255, 255}
	pixel := func(x, y int) []byte {
		i := 9 + 3*(y*320+x)
		return GetHscreenScreen()[i : i+3]
	}

	ram.trackRam[160] = 0x10
	if got := pixel(0, 1); !bytes.Equal(got, white) {
		t.Errorf("no offset: got %v", got)
	}
	ram.trackRam[160] = 0

	ram.Poke1(0xFF9F, 0x81)
	ram.trackRam[256+2] = 0x10
	if got := pixel(0, 1); !bytes.Equal(got, white) {
		t.Errorf("256-byte rows, offset 2: got %v", got)
	}
	ram.trackRam[256+2] = 0

	// Offset 254 reaches column 2 only by wrapping to the start of the row.
	ram.Poke1(0xFF9F, 0x7F)
	ram.trackRam[0] = 0x10
	if got := pixel(4, 0); !bytes.Equal(got, white) {
		t.Errorf("wrapped offset: got %v", got)
	}
}
