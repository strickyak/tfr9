package main

import (
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
