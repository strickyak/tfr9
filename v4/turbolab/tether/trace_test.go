package main

import (
	"bytes"
	"os"
	"testing"
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

