package main

import (
	"os"
	"strings"
	"sync"
	"testing"
)

func TestExecuteSaveCommandErrors(t *testing.T) {
	// Empty filename
	err := ExecuteSaveCommand("")
	if err == nil || !strings.Contains(err.Error(), "Usage: ~save") {
		t.Fatalf("expected usage error, got %v", err)
	}
}

func TestExecuteSaveCommandWithMock(t *testing.T) {
	mockRam := make([]byte, 64*1024)

	// Set zero-page for CoCo 2 Extended/Disk BASIC:
	// TXTTAB ($0019-$001A) = 0x2601
	mockRam[0x0019] = 0x26
	mockRam[0x001A] = 0x01
	// MEMSIZ ($0027-$0028) = 0x7FFE
	mockRam[0x0027] = 0x7F
	mockRam[0x0028] = 0xFE

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

	// Case 1: No program in RAM (link pointer at TXTTAB is 0x0000)
	mockRam[0x2601] = 0x00
	mockRam[0x2602] = 0x00
	mockRam[0x001B] = 0x26
	mockRam[0x001C] = 0x03
	err := ExecuteSaveCommand("/tmp/test_save.bas")
	if err == nil || !strings.Contains(err.Error(), "No program in RAM to save") {
		t.Fatalf("expected 'No program in RAM to save', got: %v", err)
	}

	// Case 2: Load a known program into mock RAM using ExecuteLoadCommand
	err = ExecuteLoadCommand("../misc/X_pmode4.bas")
	if err != nil {
		t.Fatalf("failed to load program for save test: %v", err)
	}

	// Save as pure ASCII text (.txt)
	txtPath := "/tmp/test_saved_pmode4.txt"
	defer os.Remove(txtPath)

	err = ExecuteSaveCommand(txtPath)
	if err != nil {
		t.Fatalf("ExecuteSaveCommand for .txt failed: %v", err)
	}

	txtData, err := os.ReadFile(txtPath)
	if err != nil {
		t.Fatalf("failed to read saved .txt file: %v", err)
	}

	txtContent := string(txtData)
	t.Logf("Saved ASCII file content:\n%s", txtContent)

	if !strings.Contains(txtContent, "10 PMODE 4, 1\n") {
		t.Errorf("missing line 10 in saved text file")
	}
	if !strings.Contains(txtContent, "40 LINE (0, 0)-(255, 191), PSET\n") {
		t.Errorf("missing line 40 in saved text file")
	}
	if !strings.Contains(txtContent, "60 IF INKEY$ = \"\" THEN 60\n") {
		t.Errorf("missing line 60 in saved text file")
	}

	// Verify the saved .txt file can be reloaded via LoadBasicFile
	reloadedBytes, _, numLines, err := LoadBasicFile(txtPath, 0x2601)
	if err != nil {
		t.Fatalf("failed to reload saved .txt file: %v", err)
	}
	if numLines != 6 {
		t.Errorf("expected 6 lines, got %d", numLines)
	}
	if len(reloadedBytes) == 0 {
		t.Errorf("expected non-empty reloaded bytes")
	}

	// Save as optimized tokenized binary (.bas)
	basPath := "/tmp/test_saved_pmode4.bas"
	defer os.Remove(basPath)

	err = ExecuteSaveCommand(basPath)
	if err != nil {
		t.Fatalf("ExecuteSaveCommand for .bas failed: %v", err)
	}

	basData, err := os.ReadFile(basPath)
	if err != nil {
		t.Fatalf("failed to read saved .bas file: %v", err)
	}

	// Check Disk BASIC preamble: 0xFF followed by length
	if len(basData) < 3 || basData[0] != 0xFF {
		t.Fatalf("expected Disk BASIC 0xFF preamble, got: %x", basData[:min(3, len(basData))])
	}
	expectedProgLen := len(basData) - 3
	headerLen := (int(basData[1]) << 8) | int(basData[2])
	if headerLen != expectedProgLen {
		t.Errorf("preamble length %d != actual program length %d", headerLen, expectedProgLen)
	}

	// Verify the saved .bas file can be parsed and reloaded via LoadBasicFile
	reloadedBasBytes, _, numBasLines, err := LoadBasicFile(basPath, 0x2601)
	if err != nil {
		t.Fatalf("failed to reload saved .bas file: %v", err)
	}
	if numBasLines != 6 {
		t.Errorf("expected 6 lines from .bas, got %d", numBasLines)
	}
	if len(reloadedBasBytes) != expectedProgLen {
		t.Errorf("reloaded program bytes len %d != saved progLen %d", len(reloadedBasBytes), expectedProgLen)
	}
}
