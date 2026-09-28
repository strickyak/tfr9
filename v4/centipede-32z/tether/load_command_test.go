package main

import (
	"strings"
	"sync"
	"testing"
	"time"
)

// mockFirmware handles get-ram and put-ram RPCs against an in-memory buffer.
func runMockFirmware(t *testing.T, reqCh chan []byte, respCh chan []byte, stopCh chan struct{}, ram []byte) {
	for {
		select {
		case <-stopCh:
			return
		case pkt := <-reqCh:
			if len(pkt) == 0 {
				continue
			}
			if pkt[0] == T_PICO_RPC {
				req := DecodeRpcRequest(pkt[1:])
				var resp RpcResponse
				resp.Serial = req.Serial

				switch req.Method {
				case "get-ram":
					resp.Status = 0
					offset := req.Offset
					length := req.Length
					if length == 0 {
						length = 256
					}
					if offset < len(ram) {
						end := offset + length
						if end > len(ram) {
							end = len(ram)
						}
						resp.Data = ram[offset:end]
					}
				case "put-ram":
					resp.Status = 0
					offset := req.Offset
					copy(ram[offset:], req.Data)
					resp.Size = len(req.Data)
				default:
					resp.Status = -1
					resp.Message = "unknown method"
				}

				encoded := EncodeRpcResponse(resp)
				fullPkt := append([]byte{T_PICO_RPC}, encoded...)
				respCh <- fullPkt
			}
		}
	}
}

func TestExecuteLoadCommandErrors(t *testing.T) {
	// 1. Empty args
	err := ExecuteLoadCommand("")
	if err == nil || !strings.Contains(err.Error(), "Usage: ~load") {
		t.Fatalf("expected usage error, got %v", err)
	}

	// 2. Non-existent file
	err = ExecuteLoadCommand("totally_non_existent_file_12345.bas")
	if err == nil || !strings.Contains(err.Error(), "File error") {
		t.Fatalf("expected file error, got %v", err)
	}
}

func TestExecuteLoadCommandWithMock(t *testing.T) {
	mockRam := make([]byte, 64*1024)

	// Set zero-page for standard CoCo 2 Extended/Disk BASIC:
	// TXTTAB ($0019-$001A) = 0x2601
	mockRam[0x0019] = 0x26
	mockRam[0x001A] = 0x01
	// MEMSIZ ($0027-$0028) = 0x7FFE
	mockRam[0x0027] = 0x7F
	mockRam[0x0028] = 0xFE

	// Save original global state
	oldCh := currentChannelToPico
	oldRam := the_ram
	defer func() {
		currentChannelToPico = oldCh
		the_ram = oldRam
	}()

	the_ram = new(Coco1Ram)

	toFirmware := make(chan []byte, 20)
	fromFirmware := make(chan []byte, 20)
	stopFirmware := make(chan struct{})
	defer close(stopFirmware)

	go runMockFirmware(t, toFirmware, fromFirmware, stopFirmware, mockRam)

	currentChannelToPico = toFirmware

	// Route responses from firmware into HandlePicoRpcResponse
	var stopRouter sync.WaitGroup
	stopRouter.Add(1)
	routerDone := make(chan struct{})
	go func() {
		defer stopRouter.Done()
		for {
			select {
			case <-routerDone:
				return
			case pkt := <-fromFirmware:
				if len(pkt) > 0 && pkt[0] == T_PICO_RPC {
					HandlePicoRpcResponse(pkt[1:])
				}
			}
		}
	}()
	defer func() {
		close(routerDone)
		stopRouter.Wait()
	}()

	// Case 1: Program already in RAM (link pointer at TXTTAB is non-zero)
	mockRam[0x2601] = 0x26
	mockRam[0x2602] = 0x0C
	err := ExecuteLoadCommand("../misc/Rainbow_short_hscreen2.bas")
	if err == nil || !strings.Contains(err.Error(), "Program already in RAM. Please type NEW before ~load") {
		t.Fatalf("expected 'Program already in RAM', got: %v", err)
	}

	// Case 2: Program is empty (link pointer at TXTTAB is 0x0000)
	mockRam[0x2601] = 0x00
	mockRam[0x2602] = 0x00

	err = ExecuteLoadCommand("../misc/Rainbow_short_hscreen2.bas")
	if err != nil {
		t.Fatalf("ExecuteLoadCommand failed: %v", err)
	}

	// Verify preceding byte at TXTTAB-1 ($2600) is 0x00
	if mockRam[0x2600] != 0x00 {
		t.Errorf("expected mockRam[0x2600] == 0x00, got 0x%02X", mockRam[0x2600])
	}

	// Verify first line link is non-zero
	firstLink := (uint(mockRam[0x2601]) << 8) | uint(mockRam[0x2602])
	if firstLink <= 0x2601 {
		t.Errorf("invalid first line link: 0x%04X", firstLink)
	}

	// Verify VARTAB ($001B-$001C) was updated
	vartab := (uint(mockRam[0x001B]) << 8) | uint(mockRam[0x001C])
	if vartab <= 0x2601 {
		t.Errorf("invalid VARTAB: 0x%04X", vartab)
	}

	// Verify DATPTR ($0033-$0034) is TXTTAB - 1 = 0x2600
	datptr := (uint(mockRam[0x0033]) << 8) | uint(mockRam[0x0034])
	if datptr != 0x2600 {
		t.Errorf("expected DATPTR == 0x2600, got 0x%04X", datptr)
	}

	// Verify CHARAD ($00A6-$00A7) is TXTTAB - 1 = 0x2600
	charad := (uint(mockRam[0x00A6]) << 8) | uint(mockRam[0x00A7])
	if charad != 0x2600 {
		t.Errorf("expected CHARAD == 0x2600, got 0x%04X", charad)
	}

	// Now verify we can read back and detokenize lines from mockRam
	tokenizedProgram := mockRam[0x2601:vartab]
	parsedLines, err := ParseTokenizedBasic(tokenizedProgram)
	if err != nil {
		t.Fatalf("failed to parse tokenized program from mockRam: %v", err)
	}

	if len(parsedLines) != 3 {
		t.Fatalf("expected 3 lines, got %d", len(parsedLines))
	}
	t.Logf("Parsed lines back from mockRam:")
	for _, l := range parsedLines {
		detok := DetokenizeLine(l.Tokens)
		t.Logf("  %d %s", l.LineNum, detok)
	}

	// Case 3: Test loading pure ASCII file misc/X_pmode4.bas
	// Clear RAM for NEW
	mockRam[0x2601] = 0x00
	mockRam[0x2602] = 0x00
	err = ExecuteLoadCommand("../misc/X_pmode4.bas")
	if err != nil {
		t.Fatalf("ExecuteLoadCommand for X_pmode4.bas failed: %v", err)
	}
	vartab2 := (uint(mockRam[0x001B]) << 8) | uint(mockRam[0x001C])
	tokenizedPmode := mockRam[0x2601:vartab2]
	linesPmode, err := ParseTokenizedBasic(tokenizedPmode)
	if err != nil {
		t.Fatalf("failed to parse tokenized X_pmode4: %v", err)
	}
	if len(linesPmode) != 6 {
		t.Fatalf("expected 6 lines for X_pmode4, got %d", len(linesPmode))
	}
	t.Logf("Parsed X_pmode4 lines back from mockRam:")
	for _, l := range linesPmode {
		detok := DetokenizeLine(l.Tokens)
		t.Logf("  %d %s", l.LineNum, detok)
	}

	// Case 4: Test loading misc/demo_all_modes.bas
	mockRam[0x2601] = 0x00
	mockRam[0x2602] = 0x00
	err = ExecuteLoadCommand("../misc/demo_all_modes.bas")
	if err != nil {
		t.Fatalf("ExecuteLoadCommand for demo_all_modes.bas failed: %v", err)
	}
	vartab3 := (uint(mockRam[0x001B]) << 8) | uint(mockRam[0x001C])
	tokenizedDemo := mockRam[0x2601:vartab3]
	linesDemo, err := ParseTokenizedBasic(tokenizedDemo)
	if err != nil {
		t.Fatalf("failed to parse tokenized demo_all_modes: %v", err)
	}
	t.Logf("Parsed demo_all_modes.bas lines (%d lines, %d bytes):", len(linesDemo), len(tokenizedDemo))
}

func init() {
	// Shorten timeout in tests if necessary
	_ = time.Second
}
