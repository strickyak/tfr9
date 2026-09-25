package main

import (
	"strings"
	"testing"
)

func TestParseOffset(t *testing.T) {
	tests := []struct {
		input    string
		expected uint16
		wantErr  bool
	}{
		{"", 0, false},
		{"0", 0, false},
		{"10", 10, false},
		{"16", 16, false},
		{"100", 100, false},
		{"$10", 16, false},
		{"$FE", 254, false},
		{"0x10", 16, false},
		{"0X10", 16, false},
		{"0xFE", 254, false},
		{"1a", 26, false},
		{"fe", 254, false},
		{"$1000", 4096, false},
		{"0x1000", 4096, false},
		{"xyz", 0, true},
		{"$FFFFF", 0, true},
	}

	for _, tt := range tests {
		got, err := parseOffset(tt.input)
		if (err != nil) != tt.wantErr {
			t.Errorf("parseOffset(%q) err = %v, wantErr %v", tt.input, err, tt.wantErr)
			continue
		}
		if !tt.wantErr && got != tt.expected {
			t.Errorf("parseOffset(%q) = %d ($%04X), want %d ($%04X)", tt.input, got, got, tt.expected, tt.expected)
		}
	}
}

func TestParseCount(t *testing.T) {
	tests := []struct {
		input    string
		expected uint64
		wantErr  bool
	}{
		{"0", 0, false},
		{"100", 100, false},
		{"50k", 50000, false},
		{"50K", 51200, false},
		{"64K", 65536, false},
		{"10m", 10000000, false},
		{"10M", 10485760, false},
		{"1M", 1048576, false},
		{"2g", 2000000000, false},
		{"2G", 2147483648, false},
		{"1G", 1073741824, false},
		{"$10", 16, false},
		{"0x100", 256, false},
		{"bad", 0, true},
	}

	for _, tt := range tests {
		got, err := parseCount(tt.input)
		if (err != nil) != tt.wantErr {
			t.Errorf("parseCount(%q) err = %v, wantErr %v", tt.input, err, tt.wantErr)
			continue
		}
		if !tt.wantErr && got != tt.expected {
			t.Errorf("parseCount(%q) = %d, want %d", tt.input, got, tt.expected)
		}
	}
}

func TestParseWatchpointSpec(t *testing.T) {
	tests := []struct {
		input        string
		wantType     byte
		wantAddrExpr string
		wantCount    uint32
		wantIsWp     bool
		wantErr      bool
	}{
		// Standard r/w/x formats
		{"r:0x1234", 'r', "0x1234", 1, true, false},
		{"r:0x1234:5", 'r', "0x1234", 5, true, false},
		{"w:$E000", 'w', "$E000", 1, true, false},
		{"w:$E000:10", 'w', "$E000", 10, true, false},
		{"x:4096", 'x', "4096", 1, true, false},
		{"x:0x1000:1", 'x', "0x1000", 1, true, false},
		{"r:@kernel+0x10", 'r', "@kernel+0x10", 1, true, false},
		{"r:@kernel+0x10:3", 'r', "@kernel+0x10", 3, true, false},
		{"w:@tk+$20", 'w', "@tk+$20", 1, true, false},
		{"w:@tk+$20:2", 'w', "@tk+$20", 2, true, false},
		{"x:@ioman+16", 'x', "@ioman+16", 1, true, false},
		{"x:@kernel", 'x', "@kernel", 1, true, false},
		{"x:@kernel:5", 'x', "@kernel", 5, true, false},

		// Direct @module shorthand (defaults to 'x')
		{"@kernel", 'x', "@kernel", 1, true, false},
		{"@kernel+0x10", 'x', "@kernel+0x10", 1, true, false},
		{"@kernel+$20:4", 'x', "@kernel+$20", 4, true, false},
		{"@kernel+16:2", 'x', "@kernel+16", 2, true, false},
		{"@kernel:10", 'x', "@kernel", 10, true, false},

		// Non-watchpoint flags
		{"c:1000", 0, "", 0, false, false},
		{"s:1.5s", 0, "", 0, false, false},
		{"t:500ms", 0, "", 0, false, false},

		// Invalid watchpoints
		{"r:0x1234:0", 'r', "0x1234", 0, true, true},
		{"r:0x1234:bad", 'r', "0x1234", 0, true, true},
		{"@kernel+0x10:bad", 'x', "@kernel+0x10", 0, true, true},
	}

	for _, tt := range tests {
		spec, isWp, err := parseWatchpointSpec(tt.input)
		if (err != nil) != tt.wantErr {
			t.Errorf("parseWatchpointSpec(%q) err = %v, wantErr %v", tt.input, err, tt.wantErr)
			continue
		}
		if isWp != tt.wantIsWp {
			t.Errorf("parseWatchpointSpec(%q) isWp = %v, want %v", tt.input, isWp, tt.wantIsWp)
			continue
		}
		if tt.wantIsWp && !tt.wantErr {
			if spec.Type != tt.wantType {
				t.Errorf("parseWatchpointSpec(%q) Type = %c, want %c", tt.input, spec.Type, tt.wantType)
			}
			if spec.AddrExpr != tt.wantAddrExpr {
				t.Errorf("parseWatchpointSpec(%q) AddrExpr = %q, want %q", tt.input, spec.AddrExpr, tt.wantAddrExpr)
			}
			if spec.Count != tt.wantCount {
				t.Errorf("parseWatchpointSpec(%q) Count = %d, want %d", tt.input, spec.Count, tt.wantCount)
			}
		}
	}
}

func TestResolveAddress(t *testing.T) {
	mods := []*ScannedModuleInfo{
		{Name: "kernel", FullName: "kernel.0d4eec829c", BaseAddr: 0xD4DE, Size: 0x0D4E},
		{Name: "init", FullName: "init.00361ed39b", BaseAddr: 0xE22C, Size: 0x0036},
		{Name: "tk", FullName: "tk.019526f8b2", BaseAddr: 0xE262, Size: 0x0195},
		{Name: "ioman", FullName: "ioman.070aaecac4", BaseAddr: 0xE3F7, Size: 0x070A},
	}

	tests := []struct {
		expr     string
		mods     []*ScannedModuleInfo
		expected uint16
		wantErr  bool
		errSub   string
	}{
		// Numeric literals
		{"0x1234", nil, 0x1234, false, ""},
		{"$1234", nil, 0x1234, false, ""},
		{"$E000", nil, 0xE000, false, ""},

		// Module resolution with various offset formats
		{"@kernel", mods, 0xD4DE, false, ""},
		{"@kernel+0", mods, 0xD4DE, false, ""},
		{"@kernel+0x10", mods, 0xD4EE, false, ""},
		{"@kernel+$10", mods, 0xD4EE, false, ""},
		{"@kernel+16", mods, 0xD4EE, false, ""},
		{"@KERNEL+0x20", mods, 0xD4FE, false, ""},
		{"@Kernel+0X20", mods, 0xD4FE, false, ""},
		{"@kernel-2", mods, 0xD4DC, false, ""},
		{"@init", mods, 0xE22C, false, ""},
		{"@init+0x10", mods, 0xE23C, false, ""},
		{"@tk+$20", mods, 0xE282, false, ""},
		{"@ioman+10", mods, 0xE3F7 + 10, false, ""},
		{"@kernel.0d4eec829c+0x10", mods, 0xD4EE, false, ""},
		{"@kernel.list+0x10", mods, 0xD4EE, false, ""},

		// Errors
		{"@kernel+0x10", nil, 0, true, "no OS-9 modules loaded or detected"},
		{"@kernel+0x10", []*ScannedModuleInfo{}, 0, true, "no OS-9 modules loaded or detected"},
		{"@nosuchmodule", mods, 0, true, "not found in loaded/detected OS-9 modules"},
		{"@kernel+xyz", mods, 0, true, "invalid offset"},
	}

	for _, tt := range tests {
		got, err := resolveAddress(tt.expr, tt.mods)
		if (err != nil) != tt.wantErr {
			t.Errorf("resolveAddress(%q) err = %v, wantErr %v", tt.expr, err, tt.wantErr)
			continue
		}
		if tt.wantErr {
			if tt.errSub != "" && !strings.Contains(err.Error(), tt.errSub) {
				t.Errorf("resolveAddress(%q) err = %q, want substring %q", tt.expr, err.Error(), tt.errSub)
			}
		} else {
			if got != tt.expected {
				t.Errorf("resolveAddress(%q) = $%04X, want $%04X", tt.expr, got, tt.expected)
			}
		}
	}
}
