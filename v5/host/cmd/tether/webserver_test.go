package main

import (
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"
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
	expectedLen := 9 + (320 * 192 * 3)
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
	expectedCoco2Len := 9 + (256 * 192 * 3)
	if len(screenCoco2) != expectedCoco2Len {
		t.Errorf("CoCo 2 text: expected len %d, got %d", expectedCoco2Len, len(screenCoco2))
	}
}

func TestCoco2GraphicsModesAndKeys(t *testing.T) {
	ram := new(Coco1Ram)
	the_ram = ram
	SamBits = 0
	SamPoke1(0xFFC7) // F0 = 1
	SamPoke1(0xFFC9) // F1 = 1 -> $0600

	// Test all 5 Extended Color BASIC PMODE 0..4 modes ($B0, $C0, $D0, $E0, $F8)
	for pmode := 0; pmode <= 4; pmode++ {
		p1b := byte(0x80 | ((pmode + 3) << 4) | 0x08)
		ram.Poke1(0xFF22, p1b)
		ram.Poke1(0x0600, 0xAA)
		frame := GetScreenForWebsocket()
		expectedLen := 9 + (256 * 192 * 3)
		if len(frame) != expectedLen {
			t.Fatalf("PMODE %d: expected len %d, got %d", pmode, expectedLen, len(frame))
		}
		if frame[0] != OpBitmap {
			t.Fatalf("PMODE %d: expected OpBitmap, got %d", pmode, frame[0])
		}
	}

	// Test CoCo 2 browser key mappings
	if got := KeystrokeValue(0, "Escape"); got != 27 {
		t.Errorf("Escape: expected 27, got %d", got)
	}
	if got := KeystrokeValue(1, "Escape"); got != 127 {
		t.Errorf("Shift+Escape: expected 127, got %d", got)
	}
	if got := KeystrokeValue(0, "F1"); got != 12 {
		t.Errorf("F1: expected 12 (CLEAR), got %d", got)
	}
	if got := KeystrokeValue(0, "Home"); got != 12 {
		t.Errorf("Home: expected 12 (CLEAR), got %d", got)
	}
	if got := KeystrokeValue(0, "Shift"); got != 0 {
		t.Errorf("bare Shift: expected 0, got %d", got)
	}
}

func TestCoco3MmuWriteCyclesAndGimeScreens(t *testing.T) {
	c3 := new(Coco3Ram)
	c3.gimeRomMode = true
	the_ram = c3
	SamBits = 0
	SamPoke1(0xFFC9) // F1=1 -> $0400

	// Power-on CoCo 3 Task 0 & Task 1 MMU maps + cold-boot GIME palette
	task0 := [8]byte{0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F}
	task1 := [8]byte{0x38, 0x30, 0x31, 0x32, 0x33, 0x3D, 0x35, 0x3F}
	bootPal := [16]byte{18, 36, 11, 7, 63, 31, 9, 38, 0, 18, 0, 63, 0, 18, 0, 38}
	for s := uint(0); s < 8; s++ {
		c3.Poke1(0xFFA0+s, task0[s])
		c3.Poke1(0xFFA8+s, task1[s])
	}
	for i := uint(0); i < 16; i++ {
		c3.Poke1(0xFFB0+i, bootPal[i])
	}

	// 1. PIA1 DDRB setup ($FF23=0, $FF22=$FE, $FF23=$34) must NOT corrupt Pia1OutB() into GM=7!
	c3.Poke1(0xFF22, 0x00)
	c3.Poke1(0xFF23, 0x00)
	c3.Poke1(0xFF22, 0xFE) // DDRB write!
	c3.Poke1(0xFF23, 0x34)
	if got := Pia1OutB(); got != 0x00 {
		t.Fatalf("PIA1 DDRB write clobbered Pia1OutB(): got 0x%02X, want 0x00", got)
	}

	// 2. CoCo-compatible WIDTH 32 ($FF90 = 0xC8: COCO=1, MMUEN=1, MC3=1):
	//    CPU writes to $0400 via Task 0 ($FFA0=$38 -> physical $10400).
	//    Even if CPU later maps $FFA0=$30, GIME still scans $10400!
	c3.Poke1(0xFF90, 0xC8)
	c3.Poke1(0xFF91, 0x00)
	c3.Poke1(0x0400, 0x4F) // 'O' normal black-on-green
	c3.Poke1(0x0401, 0x4B) // 'K'
	c3.Poke1(0x0402, 0xAF) // blue semigraphics-4 full block
	if c3.PPeek1(0x10400) != 0x4F || c3.PPeek1(0x10402) != 0xAF {
		t.Fatalf("expected WIDTH 32 writes at physical 0x10400..0x10402, got %02X %02X",
			c3.PPeek1(0x10400), c3.PPeek1(0x10402))
	}
	// Remap $FFA0 to $30 (Block 0, all zeros) — GIME COCO=1 must still render $10400!
	c3.Poke1(0xFFA0, 0x30)
	frame32 := GetScreenForWebsocket()
	if len(frame32) != 9+256*192*3 {
		t.Fatalf("WIDTH 32: unexpected frame length %d", len(frame32))
	}
	// Semigraphics cell at col 2 (x=16..23, y=0..11) must be Blue (50, 80, 220), not Black (0, 0, 0)
	pixOff := 9 + (0*256+16)*3
	if frame32[pixOff] != 50 || frame32[pixOff+1] != 80 || frame32[pixOff+2] != 220 {
		t.Errorf("WIDTH 32 SG4 blue pixel mismatch at $10402: got RGB(%d,%d,%d)",
			frame32[pixOff], frame32[pixOff+1], frame32[pixOff+2])
	}
	c3.Poke1(0xFFA0, 0x38)

	// 3. Constant $FE00..$FEFF page (MC3 bit 3 = 0x08) must map to $1FE00 even if $FFA7=$30!
	c3.Poke1(0xFFA7, 0x30)
	c3.Poke1(0xFE04, 40)
	if c3.PPeek1(0x1FE04) != 40 {
		t.Fatalf("MC3 $FE04 write did not land at physical 0x1FE04 (got %d)", c3.PPeek1(0x1FE04))
	}
	c3.Poke1(0xFFA7, 0x3F)

	// 4. ROM-to-RAM copy & TY=1 all-RAM writes:
	//    On CoCo 3, writes to $8000..$FDFF always pass through MMU to underlying RAM (even when TY=0),
	//    so cold boot can copy/patch Super Extended BASIC into Blocks $3C..$3F and update $E0E1..$E0F0!
	c3.Poke1(0x8000, 0x55)
	if c3.trackRam[0x18000] != 0x55 {
		t.Fatalf("RAM write under ROM ($8000 -> $18000) failed: got 0x%02X", c3.trackRam[0x18000])
	}

	// 5. Native GIME WIDTH 40 transient MMU banking:
	//    CoCo 3 ROM sets $FFA1=$36, writes char+attr to $2000/$2001 (landing at $0C000/$0C001),
	//    then restores $FFA1=$39!
	c3.Poke1(0xFF90, 0x4C)
	c3.Poke1(0xFF98, 0x03)
	c3.Poke1(0xFF99, 0x05)
	c3.Poke1(0xFF9D, 0xD8)
	c3.Poke1(0xFF9E, 0x00)

	c3.Poke1(0xFFA1, 0x36)
	c3.Poke1(0x2000, 'A')
	c3.Poke1(0x2001, 0x00)
	c3.Poke1(0xFFA1, 0x39)
	if c3.PPeek1(0x0C000) != 'A' {
		t.Fatalf("WIDTH 40 transient $FFA1=$36 write missed physical 0x0C000 (got 0x%02X)", c3.PPeek1(0x0C000))
	}
	frame40 := GetScreenForWebsocket()
	if len(frame40) != 9+320*192*3 {
		t.Fatalf("WIDTH 40: unexpected frame length %d", len(frame40))
	}

	// 6. Native GIME HSCREEN 2 transient Task 1 switch:
	//    CoCo 3 ROM sets $FF91=1, writes pixel byte to $2000 (Task 1 slot 1 = $30 -> physical $00000),
	//    then sets $FF91=0!
	c3.Poke1(0xFF98, 0x80)
	c3.Poke1(0xFF99, 0x1E) // 320x192x16
	c3.Poke1(0xFF9D, 0xC0) // 0xC000 * 8 & 0x1FFFF = 0x00000
	c3.Poke1(0xFF9E, 0x00)
	c3.Poke1(0xFF91, 0x01)
	c3.Poke1(0x2000, 0x32) // pixel 0 = Red (3), pixel 1 = Blue (2)
	c3.Poke1(0xFF91, 0x00)
	if c3.PPeek1(0x00000) != 0x32 {
		t.Fatalf("HSCREEN 2 Task 1 write missed physical 0x00000 (got 0x%02X)", c3.PPeek1(0x00000))
	}
	frameHs2 := GetScreenForWebsocket()
	if len(frameHs2) != 9+320*192*3 {
		t.Fatalf("HSCREEN 2: unexpected frame length %d", len(frameHs2))
	}
	if frameHs2[9] != 220 || frameHs2[10] != 50 || frameHs2[11] != 50 {
		t.Errorf("HSCREEN 2 pixel 0 expected Red (220,50,50), got RGB(%d,%d,%d)",
			frameHs2[9], frameHs2[10], frameHs2[11])
	}
}

func TestServeSessionAndAutoPortStep(t *testing.T) {
	defer resetBoardLabelState()
	setBoardLabel("p=1,b=centipede,v=32z,id=z99,name=zGengar")
	setActiveWebAddr(":8082", 8082)

	wcc := &WebConsoleConfig{
		Bind:    ":8080",
		Port:    8082,
		Session: "Bilbo",
	}
	mux := newWebMux(wcc)

	// 1a. Plain text GET /session with all 4 fields known
	req := httptest.NewRequest("GET", "/session", nil)
	w := httptest.NewRecorder()
	mux.ServeHTTP(w, req)
	if w.Code != http.StatusOK {
		t.Fatalf("GET /session: expected 200, got %d", w.Code)
	}
	if w.Body.String() != "8082 Bilbo z99 zGengar\n" {
		t.Errorf("GET /session: expected %q, got %q", "8082 Bilbo z99 zGengar\n", w.Body.String())
	}

	// 1b. Plain text GET /session with unknown session_name and unknown board_name -> "."
	setBoardLabel("p=1,b=centipede,v=32z,id=z99")
	muxSparse := newWebMux(&WebConsoleConfig{Bind: ":8080", Port: 8090})
	wSparse := httptest.NewRecorder()
	muxSparse.ServeHTTP(wSparse, httptest.NewRequest("GET", "/session", nil))
	if wSparse.Body.String() != "8090 . z99 .\n" {
		t.Errorf("GET /session sparse: expected %q, got %q", "8090 . z99 .\n", wSparse.Body.String())
	}

	// 2. JSON GET /session.json and /session?format=json
	setBoardLabel("p=1,b=centipede,v=32z,id=z99,name=zGengar")
	for _, path := range []string{"/session.json", "/session?format=json"} {
		reqJ := httptest.NewRequest("GET", path, nil)
		wJ := httptest.NewRecorder()
		mux.ServeHTTP(wJ, reqJ)
		if wJ.Code != http.StatusOK {
			t.Fatalf("GET %s: expected 200, got %d", path, wJ.Code)
		}
		var info SessionInfo
		if err := json.Unmarshal(wJ.Body.Bytes(), &info); err != nil {
			t.Fatalf("GET %s: invalid JSON %q: %v", path, wJ.Body.String(), err)
		}
		if !info.Tether || info.Session != "Bilbo" || info.Port != 8082 || info.BoardID != "z99" || info.BoardName != "zGengar" || info.PID <= 0 {
			t.Errorf("GET %s: unexpected SessionInfo: %+v", path, info)
		}
	}

	// 3. Concurrent GET / requests do not race on a shared strings.Reader
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			rHome := httptest.NewRequest("GET", "/", nil)
			wHome := httptest.NewRecorder()
			mux.ServeHTTP(wHome, rHome)
			if wHome.Code != http.StatusOK || !strings.Contains(wHome.Body.String(), "TFR9 CONSOLE") {
				t.Errorf("concurrent GET / failed: code=%d len=%d", wHome.Code, wHome.Body.Len())
			}
		}()
	}
	wg.Wait()

	// 4. Auto-stepping port listener + ProbeSessions (-just-list-session)
	lnForeign, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("net.Listen ephemeral failed: %v", err)
	}
	defer lnForeign.Close()
	foreignPort := lnForeign.Addr().(*net.TCPAddr).Port
	// Non-tether HTTP server returning 200 OK on /session without X-Tether-Session header must be ignored.
	srvForeign := &http.Server{Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "not a tether server")
	})}
	go srvForeign.Serve(lnForeign)
	defer srvForeign.Close()

	lnStepped, actualBind, steppedPort, err := listenWebServerRange(
		"127.0.0.1:0", false, "127.0.0.1", foreignPort, foreignPort+10,
	)
	if err != nil {
		t.Fatalf("listenWebServerRange auto-step failed: %v", err)
	}
	defer lnStepped.Close()
	if steppedPort <= foreignPort || steppedPort > foreignPort+10 {
		t.Fatalf("expected steppedPort in (%d..%d], got %d (bind=%q)", foreignPort, foreignPort+10, steppedPort, actualBind)
	}

	srvTether := &http.Server{Handler: newWebMux(&WebConsoleConfig{
		Bind:    actualBind,
		Port:    steppedPort,
		Session: "Bilbo",
	})}
	go srvTether.Serve(lnStepped)
	defer srvTether.Close()

	lines := ProbeSessions("127.0.0.1", foreignPort, steppedPort, 1*time.Second)
	wantLine := fmt.Sprintf("%d Bilbo z99 zGengar", steppedPort)
	foundWant := false
	for _, lnStr := range lines {
		if strings.HasPrefix(lnStr, fmt.Sprintf("%d ", foreignPort)) {
			t.Errorf("ProbeSessions included non-tether server on port %d: %q", foreignPort, lnStr)
		}
		if lnStr == wantLine {
			foundWant = true
		}
	}
	if !foundWant {
		t.Errorf("ProbeSessions(%d..%d) = %v; missing %q", foreignPort, steppedPort, lines, wantLine)
	}

	if _, _, _, err := listenWebServerRange(lnForeign.Addr().String(), true, "127.0.0.1", foreignPort, foreignPort+10); err == nil {
		t.Errorf("expected explicit bind on occupied address %s to fail", lnForeign.Addr().String())
	}
}


