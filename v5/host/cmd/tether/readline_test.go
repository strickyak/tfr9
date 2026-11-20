package main

import (
	"bytes"
	"path/filepath"
	"testing"
	"time"
)

func feedBytes(ed *ReadlineEditor, s string) []ReadlineResult {
	var results []ReadlineResult
	for i := 0; i < len(s); i++ {
		res := ed.HandleByte(s[i])
		if res.Action != RlActionNone {
			results = append(results, res)
		}
	}
	return results
}

func TestReadlineBasicEditAndHistory(t *testing.T) {
	var out bytes.Buffer
	ed := &ReadlineEditor{
		Out:         &out,
		MaxHistory:  100,
		HistoryFile: filepath.Join(t.TempDir(), "hist"),
	}

	// Type "echx hi", backspace 'x' after moving left, insert 'o', submit.
	feedBytes(ed, "echx hi")
	ed.HandleEscape("[D") // before 'i'
	ed.HandleEscape("[D") // before 'h'
	ed.HandleEscape("[D") // before ' '
	ed.HandleByte(127)    // delete 'x'
	ed.HandleByte('o')    // insert 'o'
	res := ed.HandleByte('\r')
	if res.Action != RlActionLine || res.Line != "echo hi" {
		t.Fatalf("got %+v, want RlActionLine 'echo hi'", res)
	}

	// Type second command "dir Cmds9", submit.
	res2 := feedBytes(ed, "dir Cmds9\n")
	if len(res2) != 1 || res2[0].Line != "dir Cmds9" {
		t.Fatalf("got %+v, want 'dir Cmds9'", res2)
	}

	// Start typing draft "partial", press Up twice -> "echo hi", Down -> "dir Cmds9", Down -> "partial"
	feedBytes(ed, "partial")
	ed.HandleEscape("[A") // "dir Cmds9"
	ed.HandleEscape("[A") // "echo hi"
	if string(ed.buf) != "echo hi" {
		t.Fatalf("after 2x Up: got %q, want 'echo hi'", string(ed.buf))
	}
	ed.HandleEscape("[B") // "dir Cmds9"
	ed.HandleEscape("[B") // "partial"
	if string(ed.buf) != "partial" {
		t.Fatalf("after 2x Down: got %q, want 'partial'", string(ed.buf))
	}

	// Ctrl-W kills "partial", yank it back with Ctrl-Y, Ctrl-A + insert "run ", submit.
	ed.HandleByte(23) // ^W
	if string(ed.buf) != "" {
		t.Fatalf("after ^W: got %q, want empty", string(ed.buf))
	}
	ed.HandleByte(25) // ^Y
	ed.HandleByte(1)  // ^A (Home)
	feedBytes(ed, "run ")
	res3 := ed.HandleByte('\r')
	if res3.Line != "run partial" {
		t.Fatalf("got %q, want 'run partial'", res3.Line)
	}

	// Verify history persisted to file and reloads in a fresh editor.
	ed2 := &ReadlineEditor{
		Out:         &out,
		MaxHistory:  100,
		HistoryFile: ed.HistoryFile,
	}
	ed2.loadHistoryFile()
	ed2.histIdx = len(ed2.History)
	if len(ed2.History) != 3 || ed2.History[0] != "echo hi" || ed2.History[2] != "run partial" {
		t.Fatalf("reloaded history = %v", ed2.History)
	}
}

func TestReadlineUCmdAndCtrlKeys(t *testing.T) {
	var out bytes.Buffer
	ed := &ReadlineEditor{Out: &out, PassCtrlC: true}

	// ~help triggers RlActionUCmd("help")
	res := feedBytes(ed, "~help\r")
	if len(res) != 1 || res[0].Action != RlActionUCmd || res[0].Line != "help" {
		t.Fatalf("got %+v, want RlActionUCmd 'help'", res)
	}

	// ~~literal sends "~literal" to target
	res = feedBytes(ed, "~~literal\r")
	if len(res) != 1 || res[0].Action != RlActionLine || res[0].Line != "~literal" {
		t.Fatalf("got %+v, want RlActionLine '~literal'", res)
	}

	// Ctrl-D on empty line sends raw EOF (4); Ctrl-C sends raw 3
	if r := ed.HandleByte(4); r.Action != RlActionRaw || r.RawByte != 4 {
		t.Fatalf("Ctrl-D on empty: got %+v", r)
	}
	feedBytes(ed, "discard me")
	if r := ed.HandleByte(3); r.Action != RlActionRaw || r.RawByte != 3 {
		t.Fatalf("Ctrl-C: got %+v", r)
	}
	if len(ed.buf) != 0 {
		t.Fatalf("buf not cleared on Ctrl-C: %q", string(ed.buf))
	}
}

func TestEchoSuppressor(t *testing.T) {
	var sup EchoSuppressor
	sup.Expect([]byte("dir\r\n"), 500*time.Millisecond)

	for _, b := range []byte("dir\r\n") {
		if !sup.ShouldDrop(b) {
			t.Fatalf("expected byte %q to be dropped", b)
		}
	}
	if sup.ShouldDrop('C') {
		t.Fatalf("unexpected drop after expected sequence consumed")
	}

	// Mismatch (e.g. target has echo OFF and immediately outputs "Cmds9") must not drop 'C'.
	sup.Expect([]byte("dir\r\n"), 500*time.Millisecond)
	if sup.ShouldDrop('C') {
		t.Fatalf("mismatch byte 'C' should not be dropped")
	}
	if sup.ShouldDrop('d') {
		t.Fatalf("expected buffer should have cleared on mismatch")
	}
}
