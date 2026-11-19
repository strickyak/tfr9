package foggynet

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func startPipeServer(t *testing.T, stateDir string) (*Server, io.Writer, io.Reader, func()) {
	t.Helper()
	rxR, rxW := io.Pipe()
	txR, txW := io.Pipe()
	srv := NewServer(rxR, txW, Options{StateDir: stateDir})
	done := make(chan struct{})
	go func() {
		_ = srv.Serve()
		close(done)
	}()
	cleanup := func() {
		_ = rxW.Close()
		<-done
		_ = txW.Close()
	}
	return srv, rxW, txR, cleanup
}

func fujiCmdRead(t *testing.T, w io.Writer, r io.Reader, req []byte, respLen int) []byte {
	t.Helper()
	// Ready
	if _, err := w.Write([]byte{0xE2, 0x00}); err != nil {
		t.Fatalf("write ready: %v", err)
	}
	var one [1]byte
	if _, err := io.ReadFull(r, one[:]); err != nil || one[0] != 0x01 {
		t.Fatalf("ready reply got %v err=%v", one[0], err)
	}
	// Command
	if _, err := w.Write(req); err != nil {
		t.Fatalf("write req: %v", err)
	}
	// SendError
	if _, err := w.Write([]byte{0xE2, 0x02}); err != nil {
		t.Fatalf("write send_error: %v", err)
	}
	if _, err := io.ReadFull(r, one[:]); err != nil || one[0] != 0x01 {
		t.Fatalf("fuji error status = 0x%02X err=%v", one[0], err)
	}
	if respLen == 0 {
		return nil
	}
	if _, err := w.Write([]byte{0xE2, 0x01}); err != nil {
		t.Fatalf("write send_response: %v", err)
	}
	out := make([]byte, respLen)
	if _, err := io.ReadFull(r, out); err != nil {
		t.Fatalf("read response: %v", err)
	}
	return out
}

func TestFujiStatusSlotsAppKeysAndLocalDisk(t *testing.T) {
	tmp := t.TempDir()
	_, w, r, cleanup := startPipeServer(t, tmp)
	defer cleanup()

	// 1. AdapterConfig (140B) + WifiStatus (1B = 3 connected)
	cfg := fujiCmdRead(t, w, r, []byte{0xE2, 0xE8}, 140)
	ssid := trimZeroString(cfg[0:33])
	if ssid != "simulated" {
		t.Fatalf("expected ssid 'simulated', got %q", ssid)
	}
	ver := trimZeroString(cfg[125:140])
	if !strings.HasPrefix(ver, "foggynet") {
		t.Fatalf("expected version 'foggynet...', got %q", ver)
	}
	ws := fujiCmdRead(t, w, r, []byte{0xE2, 0xFA}, 1)
	if ws[0] != 3 {
		t.Fatalf("expected WifiStatus=3, got %d", ws[0])
	}

	// 2. Host slots (8 * 32 = 256B) & Device slots (4 * 38 = 152B)
	hosts := fujiCmdRead(t, w, r, []byte{0xE2, 0xF4}, 256)
	if got := trimZeroString(hosts[0:32]); got != "SD" {
		t.Fatalf("expected host slot 0 'SD', got %q", got)
	}
	if got := trimZeroString(hosts[32:64]); got != "tnfs.fujinet.online" {
		t.Fatalf("expected host slot 1 'tnfs.fujinet.online', got %q", got)
	}
	devs := fujiCmdRead(t, w, r, []byte{0xE2, 0xF2}, 152)
	if len(devs) != 152 || devs[0] != 0xFF {
		t.Fatalf("unexpected initial device slots: len=%d slot0=0x%02X", len(devs), devs[0])
	}

	// 3. AppKey write + read (BE16 length)
	openAK := []byte{0xE2, 0xDC, 0x12, 0x34, 0x05, 0x01, 0x01, 0x00}
	fujiCmdRead(t, w, r, openAK, 0)
	var writeAK [2 + 2 + 64]byte
	writeAK[0] = 0xE2
	writeAK[1] = 0xDE
	binary.BigEndian.PutUint16(writeAK[2:4], 11)
	copy(writeAK[4:], "hello-foggy")
	fujiCmdRead(t, w, r, writeAK[:], 0)

	openAKRead := []byte{0xE2, 0xDC, 0x12, 0x34, 0x05, 0x01, 0x00, 0x00}
	fujiCmdRead(t, w, r, openAKRead, 0)
	akResp := fujiCmdRead(t, w, r, []byte{0xE2, 0xDD}, 66)
	if binary.BigEndian.Uint16(akResp[0:2]) != 11 || string(akResp[2:13]) != "hello-foggy" {
		t.Fatalf("appkey read mismatch: len=%d val=%q", binary.BigEndian.Uint16(akResp[0:2]), string(akResp[2:13]))
	}

	// 4. Local SD disk browse + mount + OP_READEX / OP_WRITE
	dskPath := filepath.Join(tmp, "sd", "test.dsk")
	sec0 := make([]byte, 512)
	copy(sec0[0:], "HELLO-DW-SECTOR-0")
	if err := os.WriteFile(dskPath, sec0, 0644); err != nil {
		t.Fatal(err)
	}

	var openDir [3 + 256]byte
	openDir[0] = 0xE2
	openDir[1] = 0xF7
	openDir[2] = 0 // host slot 0 = SD
	openDir[3] = '/'
	fujiCmdRead(t, w, r, openDir[:], 0)
	ent := fujiCmdRead(t, w, r, []byte{0xE2, 0xF6, 40, 0}, 40)
	if got := trimZeroString(ent); got != "test.dsk" {
		t.Fatalf("expected dir entry 'test.dsk', got %q", got)
	}
	fujiCmdRead(t, w, r, []byte{0xE2, 0xF5}, 0)

	var setDev [5 + 256]byte
	setDev[0] = 0xE2
	setDev[1] = 0xE2
	setDev[2] = 0 // device slot 0 (/X0)
	setDev[3] = 0 // host slot 0 (SD)
	setDev[4] = 2 // RW
	copy(setDev[5:], "/test.dsk")
	fujiCmdRead(t, w, r, setDev[:], 0)
	fujiCmdRead(t, w, r, []byte{0xE2, 0xF8, 0, 2}, 0)

	// OP_READEX on drive 0 LSN 0
	_, _ = w.Write([]byte{0xD2, 0, 0, 0, 0})
	gotSec := make([]byte, 256)
	if _, err := io.ReadFull(r, gotSec); err != nil {
		t.Fatal(err)
	}
	if !bytes.HasPrefix(gotSec, []byte("HELLO-DW-SECTOR-0")) {
		t.Fatalf("unexpected sector 0: %q", string(gotSec[:20]))
	}
	var csum [2]byte
	binary.BigEndian.PutUint16(csum[:], drivewireChecksum(gotSec))
	_, _ = w.Write(csum[:])
	var rc [1]byte
	_, _ = io.ReadFull(r, rc[:])
	if rc[0] != 0x00 {
		t.Fatalf("expected OP_READEX rc=0, got 0x%02X", rc[0])
	}
}

func TestNetHTTPJSONAndTCP(t *testing.T) {
	ts := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		rw.Header().Set("Content-Type", "application/json")
		fmt.Fprint(rw, `{"current":{"time":"2026-11-19T12:00","temp":62.5},"results":[{"city":"Oakland"}]}`)
	}))
	defer ts.Close()

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	go func() {
		c, acceptErr := ln.Accept()
		if acceptErr != nil {
			return
		}
		defer c.Close()
		buf := make([]byte, 64)
		n, _ := c.Read(buf)
		_, _ = c.Write([]byte("PONG:" + string(buf[:n])))
	}()

	tmp := t.TempDir()
	_, w, r, cleanup := startPipeServer(t, tmp)
	defer cleanup()

	// Open HTTP URL in unit 1, set JSON parser (0xFC aux1=1), Parse ('P'), Query ('Q') + immediate Read ('R') like fnwx
	var openPkt [5 + 256]byte
	openPkt[0] = 0xE3
	openPkt[1] = 1
	openPkt[2] = 'O'
	openPkt[3] = 4 // GET
	openPkt[4] = 0
	copy(openPkt[5:], "N:"+ts.URL)
	_, _ = w.Write(openPkt[:])
	_, _ = w.Write([]byte{0xE3, 1, 0xFC, 1, 0}) // SetParser JSON
	_, _ = w.Write([]byte{0xE3, 1, 'P', 0, 0})  // Parse

	var qPkt [5 + 256]byte
	qPkt[0] = 0xE3
	qPkt[1] = 1
	qPkt[2] = 'Q'
	copy(qPkt[5:], "/results/0/city")
	_, _ = w.Write(qPkt[:])
	_, _ = w.Write([]byte{0xE3, 1, 'R', 0, 20})  // Read up to 20 bytes
	_, _ = w.Write([]byte{0xE3, 1, 0x01, 0, 20}) // SendResponse(20)
	resp := make([]byte, 20)
	if _, err := io.ReadFull(r, resp); err != nil {
		t.Fatal(err)
	}
	if !bytes.HasPrefix(resp, []byte("Oakland\n\x00")) {
		t.Fatalf("unexpected JSON query response: %q", resp)
	}
	_, _ = w.Write([]byte{0xE3, 1, 'C', 0, 0})

	// Open TCP socket in unit 2
	var tcpOpen [5 + 256]byte
	tcpOpen[0] = 0xE3
	tcpOpen[1] = 2
	tcpOpen[2] = 'O'
	tcpOpen[3] = 12
	copy(tcpOpen[5:], "N2:tcp://"+ln.Addr().String())
	_, _ = w.Write(tcpOpen[:])
	_, _ = w.Write([]byte{0xE3, 2, 'W', 0, 4, 'P', 'I', 'N', 'G'})
	_, _ = w.Write([]byte{0xE3, 2, 'R', 0, 9})
	_, _ = w.Write([]byte{0xE3, 2, 0x01, 0, 9})
	tcpResp := make([]byte, 9)
	if _, err := io.ReadFull(r, tcpResp); err != nil {
		t.Fatal(err)
	}
	if string(tcpResp) != "PONG:PING" {
		t.Fatalf("unexpected TCP response: %q", string(tcpResp))
	}
}
