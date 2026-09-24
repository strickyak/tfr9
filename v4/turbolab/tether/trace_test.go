package main

import (
	"bytes"
	"os"
	"strings"
	"testing"
	"time"
)

func TestTraceFormatterFIC(t *testing.T) {
	buf := &bytes.Buffer{}
	tf := &TraceFormatter{
		Out: buf,
		Listings: MultiListings{
			&ListingSource{
				BaseAddr:    0xE15F,
				ModSize:     0x0200,
				IsOs9Module: true,
				FullName:    "krn.0123896745",
				Src: map[uint16]string{
					0xE2B4: "tfr a,dp set direct page to zero",
				},
			},
			&ListingSource{
				BaseAddr: 0x0000,
				Src: map[uint16]string{
					0xFFFE: "fdb ResetEntry",
				},
			},
		},
		Modules: []*ScannedModuleInfo{
			{
				Name:     "Krn",
				FullName: "krn.0123896745",
				BaseAddr: 0xE15F,
				Size:     0x0200,
			},
			{
				Name:     "Init",
				FullName: "init.00361ed39b",
				BaseAddr: 0xE400,
				Size:     0x0036,
			},
		},
		TraceBitmask: TRACE_X,
	}

	// Case 1: Known module, known offset, assembly source present
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xE2B4, 0x1F, 14450621)
	expected1 := "x E2B4 1F #14450621; \"krn.0123896745\"+0155 tfr a,dp set direct page to zero\n"
	if got := buf.String(); got != expected1 {
		t.Errorf("Case 1 mismatch:\n got: %q\nwant: %q", got, expected1)
	}

	// Case 2: Known module, known offset, NO assembly source
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xE410, 0x20, 14450630)
	expected2 := "x E410 20 #14450630; \"init.00361ed39b\"+0010\n"
	if got := buf.String(); got != expected2 {
		t.Errorf("Case 2 mismatch:\n got: %q\nwant: %q", got, expected2)
	}

	// Case 3: Unknown module, assembly source present
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xFFFE, 0x12, 100)
	expected3 := "x FFFE 12 #100; fdb ResetEntry\n"
	if got := buf.String(); got != expected3 {
		t.Errorf("Case 3 mismatch:\n got: %q\nwant: %q", got, expected3)
	}

	// Case 4: Unknown module, NO assembly source
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0x0400, 0x39, 200)
	expected4 := "x 0400 39 #200; \n"
	if got := buf.String(); got != expected4 {
		t.Errorf("Case 4 mismatch:\n got: %q\nwant: %q", got, expected4)
	}
}

func TestListingLabels(t *testing.T) {
	// Sample listing lines matching lwasm format with user example offsets
	listingData := `
0051 4F               (      tkt9sim.asm):00110         SvcIRQ clra clear A
0052 1F8B             (      tkt9sim.asm):00111          tfr a,dp set direct page to zero
0054 B6FF02           (      tkt9sim.asm):00113          lda MappedIOStart+Reg.Stat get the status register
`
	tmpfile, err := os.CreateTemp("", "test_listing_*.list")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(tmpfile.Name())
	if _, err := tmpfile.WriteString(listingData); err != nil {
		t.Fatal(err)
	}
	tmpfile.Close()

	src, err := LoadListingFile(tmpfile.Name(), 0xE262)
	if err != nil {
		t.Fatalf("LoadListingFile failed: %v", err)
	}

	src.IsOs9Module = true
	src.FullName = "tk.019526f8b2"
	src.ModSize = 0x0195

	buf := &bytes.Buffer{}
	tf := &TraceFormatter{
		Out:          buf,
		Listings:     MultiListings{src},
		TraceBitmask: TRACE_X,
	}

	// Line 1: SvcIRQ (has label -> single space before SvcIRQ)
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xE2B3, 0x4F, 8690269)
	exp1 := "x E2B3 4F #8690269; \"tk.019526f8b2\"+0051 SvcIRQ clra clear A\n"
	if got := buf.String(); got != exp1 {
		t.Errorf("Line 1 mismatch:\n got: %q\nwant: %q", got, exp1)
	}

	// Line 2: tfr (no label -> two extra spaces before tfr)
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xE2B4, 0x1F, 8690271)
	exp2 := "x E2B4 1F #8690271; \"tk.019526f8b2\"+0052   tfr a,dp set direct page to zero\n"
	if got := buf.String(); got != exp2 {
		t.Errorf("Line 2 mismatch:\n got: %q\nwant: %q", got, exp2)
	}

	// Line 3: lda (no label -> two extra spaces before lda)
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xE2B6, 0xB6, 8690277)
	exp3 := "x E2B6 B6 #8690277; \"tk.019526f8b2\"+0054   lda MappedIOStart+Reg.Stat get the status register\n"
	if got := buf.String(); got != exp3 {
		t.Errorf("Line 3 mismatch:\n got: %q\nwant: %q", got, exp3)
	}
}

func TestTraceFormatterFlags(t *testing.T) {
	buf := &bytes.Buffer{}
	tf := &TraceFormatter{
		Out:          buf,
		TraceBitmask: 0xFF,
	}

	// 1. FLAG_LIC on KIND_OPCODE_CONT (no comment) -> ";_"
	buf.Reset()
	tf.FormatCycle(KIND_OPCODE_CONT|FLAG_LIC, 0xD4F4, 0x20, 11)
	exp1 := "+ D4F4 20 #11;_\n"
	if got := buf.String(); got != exp1 {
		t.Errorf("Mismatch for FLAG_LIC:\n got: %q\nwant: %q", got, exp1)
	}

	// 2. FLAG_BS on reset vector read (with comment) -> ";s reset vector (high)"
	buf.Reset()
	tf.FormatCycle(KIND_READ|FLAG_BS, 0xFFFE, 0xD4, 1)
	exp2 := "r FFFE D4 #1;s reset vector (high)\n"
	if got := buf.String(); got != exp2 {
		t.Errorf("Mismatch for FLAG_BS with comment:\n got: %q\nwant: %q", got, exp2)
	}

	// 3. All flags (BA, BS, LIC, BUSY) -> "as_y"
	buf.Reset()
	tf.FormatCycle(KIND_WRITE|FLAG_BA|FLAG_BS|FLAG_LIC|FLAG_BUSY, 0x0400, 0x55, 42)
	exp3 := "w 0400 55 #42;as_y\n"
	if got := buf.String(); got != exp3 {
		t.Errorf("Mismatch for all flags:\n got: %q\nwant: %q", got, exp3)
	}

	// 4. No flags (legacy format preserved with trailing space)
	buf.Reset()
	tf.FormatCycle(KIND_OPCODE_CONT, 0xD4F4, 0x20, 11)
	exp4 := "+ D4F4 20 #11; \n"
	if got := buf.String(); got != exp4 {
		t.Errorf("Mismatch for no flags:\n got: %q\nwant: %q", got, exp4)
	}
}

func TestTraceFormatterInterrupts(t *testing.T) {
	buf := &bytes.Buffer{}
	tf := &TraceFormatter{
		Out:          buf,
		TraceBitmask: TRACE_I, // ONLY 'i' enabled!
	}

	// 1. IRQ vector read (high byte at $FFF8 with BS flag) -> tagged with 'r'
	buf.Reset()
	tf.FormatCycle(KIND_READ|FLAG_BS, 0xFFF8, 0x01, 500)
	exp1 := "r FFF8 01 #500;s IRQ vector (high)\n"
	if got := buf.String(); got != exp1 {
		t.Errorf("IRQ high mismatch:\n got: %q\nwant: %q", got, exp1)
	}

	// 2. IRQ vector read (low byte at $FFF9 with BS flag) -> tagged with 'r'
	buf.Reset()
	tf.FormatCycle(KIND_READ|FLAG_BS, 0xFFF9, 0x0C, 501)
	exp2 := "r FFF9 0C #501;s IRQ vector (low)\n"
	if got := buf.String(); got != exp2 {
		t.Errorf("IRQ low mismatch:\n got: %q\nwant: %q", got, exp2)
	}

	// 3. RTI instruction fetch (opcode $3B) -> tagged with 'x'
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0xD456, 0x3B, 550)
	exp3 := "x D456 3B #550; rti\n"
	if got := buf.String(); got != exp3 {
		t.Errorf("RTI mismatch:\n got: %q\nwant: %q", got, exp3)
	}

	// 4. Non-interrupt read (e.g. normal RAM read at $0200) -> must be suppressed
	buf.Reset()
	tf.FormatCycle(KIND_READ, 0x0200, 0x12, 510)
	if got := buf.String(); got != "" {
		t.Errorf("Normal read should be suppressed under TRACE_I, got: %q", got)
	}

	// 5. Non-RTI opcode fetch (e.g. NOP at $0200) -> must be suppressed
	buf.Reset()
	tf.FormatCycle(KIND_FIC, 0x0200, 0x12, 511)
	if got := buf.String(); got != "" {
		t.Errorf("Normal opcode should be suppressed under TRACE_I, got: %q", got)
	}
}

func TestTraceFormatterIdle(t *testing.T) {
	buf := &bytes.Buffer{}
	tf := &TraceFormatter{
		Out:          buf,
		TraceBitmask: TRACE_IDLE, // ONLY '-' enabled!
	}

	// 1. Idle cycle without flags -> '- ---- -- #3; '
	buf.Reset()
	tf.FormatCycle(KIND_IDLE, 0xFFFF, 0x00, 3)
	exp1 := "- ---- -- #3; \n"
	if got := buf.String(); got != exp1 {
		t.Errorf("Idle without flags mismatch:\n got: %q\nwant: %q", got, exp1)
	}

	// 2. Idle cycle with flags (e.g. LIC) -> '- ---- -- #4;_'
	buf.Reset()
	tf.FormatCycle(KIND_IDLE|FLAG_LIC, 0xFFFF, 0x00, 4)
	exp2 := "- ---- -- #4;_\n"
	if got := buf.String(); got != exp2 {
		t.Errorf("Idle with FLAG_LIC mismatch:\n got: %q\nwant: %q", got, exp2)
	}

	// 3. Normal read suppressed when only TRACE_IDLE enabled
	buf.Reset()
	tf.FormatCycle(KIND_READ, 0x0200, 0x12, 5)
	if got := buf.String(); got != "" {
		t.Errorf("Normal read should be suppressed under TRACE_IDLE, got: %q", got)
	}
}

func TestParseTraceFlags(t *testing.T) {
	allExceptIdle := TRACE_X | TRACE_PLUS | TRACE_R | TRACE_W | TRACE_I | TRACE_T
	allWithIdle := allExceptIdle | TRACE_IDLE

	tests := []struct {
		input       string
		wantMask    int
		wantErr     bool
	}{
		{"", 0, false},
		{"1", allExceptIdle, false},
		{"all", allExceptIdle, false},
		{"1,idle", allWithIdle, false},
		{"1,-", allWithIdle, false},
		{"all,idle", allWithIdle, false},
		{"all,-", allWithIdle, false},
		{"-", TRACE_IDLE, false},
		{"idle", TRACE_IDLE, false},
		{"x", TRACE_X, false},
		{"x,w", TRACE_X | TRACE_W, false},
		{"foo", 0, true},
	}

	for _, tc := range tests {
		got, err := parseTraceFlags(tc.input)
		if tc.wantErr {
			if err == nil {
				t.Errorf("parseTraceFlags(%q) expected error, got nil", tc.input)
			}
		} else {
			if err != nil {
				t.Errorf("parseTraceFlags(%q) unexpected error: %v", tc.input, err)
			}
			if got != tc.wantMask {
				t.Errorf("parseTraceFlags(%q) = 0x%02X, want 0x%02X", tc.input, got, tc.wantMask)
			}
			if tc.input == "1" || tc.input == "all" {
				if got&TRACE_IDLE != 0 {
					t.Errorf("parseTraceFlags(%q) should NOT include TRACE_IDLE", tc.input)
				}
			}
		}
	}
}

func TestCycleSpeedEstimator(t *testing.T) {
	// 1. Disabled estimator does nothing
	disabled := NewCycleSpeedEstimator(false)
	disabled.OnCycle(100)
	if _, _, _, ok := disabled.Calculate(); ok {
		t.Errorf("Disabled estimator should return ok=false")
	}

	// 2. Enabled estimator with single cycle
	baseTime := time.Date(2026, 9, 23, 12, 0, 0, 0, time.UTC)
	currentTime := baseTime
	timeHook := func() time.Time { return currentTime }

	e := NewCycleSpeedEstimator(true)
	e.nowFunc = timeHook
	e.OnCycle(10)
	// Same cycle or no advance
	if _, _, _, ok := e.Calculate(); ok {
		t.Errorf("Single cycle should not yield speed estimate")
	}

	// 3. Normal cycle progression
	currentTime = baseTime.Add(2 * time.Second)
	e.OnCycle(3260010) // 3,260,000 cycles in 2 seconds = 1.63 MHz

	cps, totalCycles, elapsed, ok := e.Calculate()
	if !ok {
		t.Fatalf("Expected Calculate() to succeed")
	}
	if totalCycles != 3260000 {
		t.Errorf("totalCycles = %d, want 3260000", totalCycles)
	}
	if elapsed != 2*time.Second {
		t.Errorf("elapsed = %v, want 2s", elapsed)
	}
	if cps != 1630000.0 {
		t.Errorf("cps = %f, want 1630000.0", cps)
	}

	// 4. Test PrintReport outputs to Stderr and Stdout
	stderrBuf := &bytes.Buffer{}
	stdoutBuf := &bytes.Buffer{}
	e.Stderr = stderrBuf
	e.Stdout = stdoutBuf

	e.PrintReport()
	if !strings.Contains(stderrBuf.String(), "Estimated cycles per second: 1630000 (1.630 MHz)") {
		t.Errorf("stderr missing expected estimate, got: %q", stderrBuf.String())
	}
	if !strings.Contains(stdoutBuf.String(), "Estimated cycles per second: 1630000 (1.630 MHz)") {
		t.Errorf("stdout missing expected estimate, got: %q", stdoutBuf.String())
	}

	// 5. Test idempotency (PrintReport only prints once)
	stderrLen := stderrBuf.Len()
	e.PrintReport()
	if stderrBuf.Len() != stderrLen {
		t.Errorf("PrintReport should only print once, got extra output")
	}
}

func TestFormatCC(t *testing.T) {
	tests := []struct {
		cc   byte
		want string
	}{
		{0x00, "........"},
		{0xFF, "EFHINZVC"},
		{0xD0, "EF.I...."},
		{0x81, "E......C"},
		{0x04, ".....Z.."},
	}

	for _, tc := range tests {
		got := formatCC(tc.cc)
		if got != tc.want {
			t.Errorf("formatCC(0x%02X) = %q, want %q", tc.cc, got, tc.want)
		}
	}
}
