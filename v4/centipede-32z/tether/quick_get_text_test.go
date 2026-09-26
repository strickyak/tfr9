package main

import (
	"testing"
)

func TestParseQuickGetTextArg(t *testing.T) {
	tests := []struct {
		arg        string
		wantAddr   int
		wantWidth  int
		wantHeight int
	}{
		{"0x0400,32,16", 0x0400, 32, 16},
		{"z", 0x0400, 32, 16},
		{"0x0800", 0x0800, 32, 16},
		{"0x0400,32", 0x0400, 32, 16},
		{"0x0400,40,25", 0x0400, 40, 25},
		{"$0400", 0x0400, 32, 16},
		{"$0800,32,16", 0x0800, 32, 16},
		{"1024", 1024, 32, 16},
		{"1024,32,16", 1024, 32, 16},
		{"", 0x0400, 32, 16},
		{"foo", 0x0400, 32, 16},
		{"0x0400,foo", 0x0400, 32, 16},
		{"0x0400,32,bar", 0x0400, 32, 16},
	}

	for _, tt := range tests {
		gotAddr, gotWidth, gotHeight := parseQuickGetTextArg(tt.arg)
		if gotAddr != tt.wantAddr || gotWidth != tt.wantWidth || gotHeight != tt.wantHeight {
			t.Errorf("parseQuickGetTextArg(%q) = (%d, %d, %d); want (%d, %d, %d)",
				tt.arg, gotAddr, gotWidth, gotHeight, tt.wantAddr, tt.wantWidth, tt.wantHeight)
		}
	}
}

func TestDecodeVdgByte(t *testing.T) {
	tests := []struct {
		in   byte
		want byte
	}{
		{0x00, '@'},
		{0x01, 'A'},
		{0x1A, 'Z'},
		{0x1B, '['},
		{0x1F, '_'},
		{0x20, ' '},
		{0x21, '!'},
		{0x30, '0'},
		{0x39, '9'},
		{0x3F, '?'},
		// Inverse characters (bit 6 set)
		{0x40, '@'},
		{0x41, 'A'},
		{0x5A, 'Z'},
		{0x60, ' '},
	}

	for _, tt := range tests {
		got := decodeVdgByte(tt.in)
		if got != tt.want {
			t.Errorf("decodeVdgByte(0x%02X) = %q (0x%02X); want %q (0x%02X)",
				tt.in, got, got, tt.want, tt.want)
		}
	}
}
