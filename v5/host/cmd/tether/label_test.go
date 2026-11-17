package main

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestValidateLabelString(t *testing.T) {
	valid := []string{
		"p=1,b=tfr9,v=911h,mhz=250,id=uks",
		"p=1,b=tfr9,v=911h,mhz=250,id=t1,name=MyBoard",
		"p=1,b=centipede,v=32z,mhz=250,name=Vinelix,id=z10",
		"p=1,emptyval=",
	}
	for _, s := range valid {
		if err := ValidateLabelString(s); err != nil {
			t.Errorf("ValidateLabelString(%q) unexpected error: %v", s, err)
		}
	}

	invalid := []string{
		"",
		"b=tfr9,v=911h",
		"p=2,b=tfr9,v=911h",
		"p=1,",
		"p=1,noequals",
		"p=1,=nokey",
		"p=1,a=b=c",
		"p=1,bad=\x00byte",
		"p=1,bad=\xffbyte",
		"p=1,long=" + strings.Repeat("x", 260),
	}
	for _, s := range invalid {
		if err := ValidateLabelString(s); err == nil {
			t.Errorf("ValidateLabelString(%q) expected error, got nil", s)
		}
	}
}

func TestMakeUf2Label(t *testing.T) {
	out := filepath.Join(t.TempDir(), "_metadata.uf2")
	lbl := "p=1,b=tfr9,v=911h,mhz=250,id=test1,name=Desk911"
	MakeUf2Label(lbl, out)

	b, err := os.ReadFile(out)
	if err != nil {
		t.Fatalf("ReadFile: %v", err)
	}
	if len(b) != LABEL_BLOCK_SIZE {
		t.Fatalf("len = %d, want %d", len(b), LABEL_BLOCK_SIZE)
	}
	if got := binary.LittleEndian.Uint32(b[12:16]); got != LABEL_TARGET_ADDR {
		t.Fatalf("targetAddr = 0x%08X, want 0x%08X", got, LABEL_TARGET_ADDR)
	}
	if got := binary.LittleEndian.Uint32(b[28:32]); got != RP2350_ARM_FAMILY_ID {
		t.Fatalf("familyID = 0x%08X, want 0x%08X", got, RP2350_ARM_FAMILY_ID)
	}
	payload := string(b[32 : 32+len(lbl)])
	if payload != lbl {
		t.Fatalf("payload = %q, want %q", payload, lbl)
	}
	if b[32+len(lbl)] != 0 {
		t.Fatalf("expected NUL padding after payload, got 0x%02X", b[32+len(lbl)])
	}
}
