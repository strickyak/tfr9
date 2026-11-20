package main

import (
	"bufio"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"
)

// EchoSuppressor absorbs echoed command bytes coming back over C_PUTCHAR
// when tether itself (in -readline mode or interactive TTY -cooked mode)
// has already displayed the typed line locally.
type EchoSuppressor struct {
	mu       sync.Mutex
	expected []byte
	deadline time.Time
}

var lineEchoSuppressor EchoSuppressor

func (e *EchoSuppressor) Expect(b []byte, ttl time.Duration) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.expected = append(e.expected[:0], b...)
	e.deadline = time.Now().Add(ttl)
}

func (e *EchoSuppressor) Clear() {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.expected = nil
}

func (e *EchoSuppressor) ShouldDrop(ch byte) bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	if len(e.expected) == 0 {
		return false
	}
	if time.Now().After(e.deadline) {
		e.expected = nil
		return false
	}
	if ch == e.expected[0] {
		e.expected = e.expected[1:]
		return true
	}
	// If expected had "\r\n" left and target echoed only '\n' (or vice versa), absorb it cleanly.
	if len(e.expected) == 2 && e.expected[0] == '\r' && e.expected[1] == '\n' && ch == '\n' {
		e.expected = nil
		return true
	}
	e.expected = nil
	return false
}

func isStdinTTY() bool {
	fi, err := os.Stdin.Stat()
	if err != nil {
		return false
	}
	return (fi.Mode() & os.ModeCharDevice) != 0
}

// ReadlineAction describes what ReadlineEditor produced after consuming a key/sequence.
type ReadlineAction int

const (
	RlActionNone ReadlineAction = iota
	RlActionLine                // Submitted cooked line (Line holds text without CR/LF)
	RlActionUCmd                // Submitted ~command for tether (Line holds command after '~')
	RlActionRaw                 // Immediate control byte to send to target (RawByte: e.g. ^C=3, ^D=4)
	RlActionQuit                // Quit tether (^] when -pass_ctrl_c is active)
)

type ReadlineResult struct {
	Action  ReadlineAction
	Line    string
	RawByte byte
}

// ReadlineEditor implements bash/emacs-style single-line editing and history
// relative to the current cursor position so target prompts are preserved.
type ReadlineEditor struct {
	Out         io.Writer
	PassCtrlC   bool
	History     []string
	MaxHistory  int
	HistoryFile string

	buf         []byte
	pos         int
	renderedLen int
	renderedPos int

	histIdx int
	draft   string
	killBuf []byte
}

func NewReadlineEditor(out io.Writer, passCtrlC bool) *ReadlineEditor {
	ed := &ReadlineEditor{
		Out:        out,
		PassCtrlC:  passCtrlC,
		MaxHistory: 500,
	}
	if home, err := os.UserHomeDir(); err == nil && home != "" {
		ed.HistoryFile = filepath.Join(home, ".tether_history")
		ed.loadHistoryFile()
	}
	ed.histIdx = len(ed.History)
	return ed
}

func (ed *ReadlineEditor) loadHistoryFile() {
	if ed.HistoryFile == "" {
		return
	}
	f, err := os.Open(ed.HistoryFile)
	if err != nil {
		return
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if line != "" {
			ed.addHistoryInMemory(line)
		}
	}
}

func (ed *ReadlineEditor) addHistoryInMemory(line string) {
	if line == "" || strings.HasPrefix(line, " ") {
		return
	}
	if len(ed.History) > 0 && ed.History[len(ed.History)-1] == line {
		return
	}
	ed.History = append(ed.History, line)
	if ed.MaxHistory > 0 && len(ed.History) > ed.MaxHistory {
		ed.History = ed.History[len(ed.History)-ed.MaxHistory:]
	}
}

func (ed *ReadlineEditor) recordHistory(line string) {
	if line == "" || strings.HasPrefix(line, " ") {
		ed.histIdx = len(ed.History)
		ed.draft = ""
		return
	}
	prevLen := len(ed.History)
	lastSame := prevLen > 0 && ed.History[prevLen-1] == line
	ed.addHistoryInMemory(line)
	ed.histIdx = len(ed.History)
	ed.draft = ""
	if !lastSame && ed.HistoryFile != "" {
		if f, err := os.OpenFile(ed.HistoryFile, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0600); err == nil {
			_, _ = f.WriteString(line + "\n")
			_ = f.Close()
		}
	}
}

func (ed *ReadlineEditor) writeBytes(b []byte) {
	if ed.Out != nil && len(b) > 0 {
		_, _ = ed.Out.Write(b)
	}
}

func (ed *ReadlineEditor) redraw() {
	var out []byte
	if ed.renderedPos > 0 {
		for i := 0; i < ed.renderedPos; i++ {
			out = append(out, '\b')
		}
	}
	out = append(out, ed.buf...)
	if len(ed.buf) < ed.renderedLen {
		diff := ed.renderedLen - len(ed.buf)
		for i := 0; i < diff; i++ {
			out = append(out, ' ')
		}
		for i := 0; i < diff; i++ {
			out = append(out, '\b')
		}
	}
	if ed.pos < len(ed.buf) {
		back := len(ed.buf) - ed.pos
		for i := 0; i < back; i++ {
			out = append(out, '\b')
		}
	}
	ed.writeBytes(out)
	ed.renderedLen = len(ed.buf)
	ed.renderedPos = ed.pos
}

func (ed *ReadlineEditor) resetLine() {
	ed.buf = ed.buf[:0]
	ed.pos = 0
	ed.renderedLen = 0
	ed.renderedPos = 0
	ed.histIdx = len(ed.History)
	ed.draft = ""
}

func (ed *ReadlineEditor) setLine(s string) {
	ed.buf = append(ed.buf[:0], []byte(s)...)
	ed.pos = len(ed.buf)
	ed.redraw()
}

func (ed *ReadlineEditor) HandleEscape(seq string) ReadlineResult {
	switch seq {
	case "[A", "OA": // Up arrow -> previous history
		ed.historyPrev()
	case "[B", "OB": // Down arrow -> next history
		ed.historyNext()
	case "[C", "OC": // Right arrow
		if ed.pos < len(ed.buf) {
			ed.pos++
			ed.redraw()
		}
	case "[D", "OD": // Left arrow
		if ed.pos > 0 {
			ed.pos--
			ed.redraw()
		}
	case "[H", "OH", "[1~", "[7~": // Home
		if ed.pos > 0 {
			ed.pos = 0
			ed.redraw()
		}
	case "[F", "OF", "[4~", "[8~": // End
		if ed.pos < len(ed.buf) {
			ed.pos = len(ed.buf)
			ed.redraw()
		}
	case "[3~": // Delete key
		if ed.pos < len(ed.buf) {
			ed.buf = append(ed.buf[:ed.pos], ed.buf[ed.pos+1:]...)
			ed.redraw()
		}
	case "b", "B", "[1;5D", "[1;3D": // Alt-B / Ctrl-Left: backward word
		ed.wordBackward()
	case "f", "F", "[1;5C", "[1;3C": // Alt-F / Ctrl-Right: forward word
		ed.wordForward()
	case "d", "D": // Alt-D: kill word forward
		ed.killWordForward()
	}
	return ReadlineResult{Action: RlActionNone}
}

func (ed *ReadlineEditor) historyPrev() {
	if len(ed.History) == 0 || ed.histIdx <= 0 {
		return
	}
	if ed.histIdx == len(ed.History) {
		ed.draft = string(ed.buf)
	}
	ed.histIdx--
	ed.setLine(ed.History[ed.histIdx])
}

func (ed *ReadlineEditor) historyNext() {
	if ed.histIdx >= len(ed.History) {
		return
	}
	ed.histIdx++
	if ed.histIdx == len(ed.History) {
		ed.setLine(ed.draft)
	} else {
		ed.setLine(ed.History[ed.histIdx])
	}
}

func isWordByte(c byte) bool {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
}

func (ed *ReadlineEditor) wordBackward() {
	p := ed.pos
	for p > 0 && !isWordByte(ed.buf[p-1]) {
		p--
	}
	for p > 0 && isWordByte(ed.buf[p-1]) {
		p--
	}
	if p != ed.pos {
		ed.pos = p
		ed.redraw()
	}
}

func (ed *ReadlineEditor) wordForward() {
	p := ed.pos
	n := len(ed.buf)
	for p < n && !isWordByte(ed.buf[p]) {
		p++
	}
	for p < n && isWordByte(ed.buf[p]) {
		p++
	}
	if p != ed.pos {
		ed.pos = p
		ed.redraw()
	}
}

func (ed *ReadlineEditor) killWordBackward() {
	if ed.pos == 0 {
		return
	}
	p := ed.pos
	for p > 0 && ed.buf[p-1] == ' ' {
		p--
	}
	for p > 0 && ed.buf[p-1] != ' ' {
		p--
	}
	ed.killBuf = append(ed.killBuf[:0], ed.buf[p:ed.pos]...)
	ed.buf = append(ed.buf[:p], ed.buf[ed.pos:]...)
	ed.pos = p
	ed.redraw()
}

func (ed *ReadlineEditor) killWordForward() {
	n := len(ed.buf)
	if ed.pos >= n {
		return
	}
	p := ed.pos
	for p < n && !isWordByte(ed.buf[p]) {
		p++
	}
	for p < n && isWordByte(ed.buf[p]) {
		p++
	}
	ed.killBuf = append(ed.killBuf[:0], ed.buf[ed.pos:p]...)
	ed.buf = append(ed.buf[:ed.pos], ed.buf[p:]...)
	ed.redraw()
}

func (ed *ReadlineEditor) HandleByte(ch byte) ReadlineResult {
	if ed.PassCtrlC && ch == 0x1D { // ^] quits tether when -pass_ctrl_c is active
		return ReadlineResult{Action: RlActionQuit}
	}

	switch ch {
	case '\r', '\n':
		// Move cursor to end of line before emitting CRLF so wrapped/mid-line edits stay intact.
		if ed.pos < len(ed.buf) {
			ed.pos = len(ed.buf)
			ed.redraw()
		}
		line := string(ed.buf)
		ed.writeBytes([]byte{'\r', '\n'})
		ed.recordHistory(line)
		ed.resetLine()

		if line == "^C" || line == "^c" {
			return ReadlineResult{Action: RlActionRaw, RawByte: 3}
		}
		if strings.HasPrefix(line, "~") {
			if strings.HasPrefix(line, "~~") {
				return ReadlineResult{Action: RlActionLine, Line: line[1:]}
			}
			return ReadlineResult{Action: RlActionUCmd, Line: line[1:]}
		}
		return ReadlineResult{Action: RlActionLine, Line: line}

	case 3: // Ctrl-C
		if ed.pos < len(ed.buf) {
			ed.pos = len(ed.buf)
			ed.redraw()
		}
		ed.writeBytes([]byte("^C\r\n"))
		ed.resetLine()
		return ReadlineResult{Action: RlActionRaw, RawByte: 3}

	case 4: // Ctrl-D: EOF on empty line, Delete-char-under-cursor on non-empty line
		if len(ed.buf) == 0 {
			ed.resetLine()
			return ReadlineResult{Action: RlActionRaw, RawByte: 4}
		}
		if ed.pos < len(ed.buf) {
			ed.buf = append(ed.buf[:ed.pos], ed.buf[ed.pos+1:]...)
			ed.redraw()
		}

	case 8, 127: // Backspace / DEL
		if ed.pos > 0 {
			ed.buf = append(ed.buf[:ed.pos-1], ed.buf[ed.pos:]...)
			ed.pos--
			ed.redraw()
		}

	case 1: // Ctrl-A (Home)
		if ed.pos > 0 {
			ed.pos = 0
			ed.redraw()
		}

	case 5: // Ctrl-E (End)
		if ed.pos < len(ed.buf) {
			ed.pos = len(ed.buf)
			ed.redraw()
		}

	case 2: // Ctrl-B (Left)
		if ed.pos > 0 {
			ed.pos--
			ed.redraw()
		}

	case 6: // Ctrl-F (Right)
		if ed.pos < len(ed.buf) {
			ed.pos++
			ed.redraw()
		}

	case 16: // Ctrl-P (Up / prev history)
		ed.historyPrev()

	case 14: // Ctrl-N (Down / next history)
		ed.historyNext()

	case 11: // Ctrl-K (Kill to end of line)
		if ed.pos < len(ed.buf) {
			ed.killBuf = append(ed.killBuf[:0], ed.buf[ed.pos:]...)
			ed.buf = ed.buf[:ed.pos]
			ed.redraw()
		}

	case 21, 24: // Ctrl-U / Ctrl-X (Kill to start of line)
		if ed.pos > 0 {
			ed.killBuf = append(ed.killBuf[:0], ed.buf[:ed.pos]...)
			ed.buf = append(ed.buf[:0], ed.buf[ed.pos:]...)
			ed.pos = 0
			ed.redraw()
		}

	case 23: // Ctrl-W (Kill previous word)
		ed.killWordBackward()

	case 25: // Ctrl-Y (Yank killed text)
		if len(ed.killBuf) > 0 {
			tail := append([]byte(nil), ed.buf[ed.pos:]...)
			ed.buf = append(ed.buf[:ed.pos], ed.killBuf...)
			ed.buf = append(ed.buf, tail...)
			ed.pos += len(ed.killBuf)
			ed.redraw()
		}

	case 20: // Ctrl-T (Transpose characters)
		if len(ed.buf) >= 2 && ed.pos > 0 {
			idx := ed.pos
			if idx == len(ed.buf) {
				idx--
			}
			ed.buf[idx-1], ed.buf[idx] = ed.buf[idx], ed.buf[idx-1]
			if ed.pos < len(ed.buf) {
				ed.pos++
			}
			ed.redraw()
		}

	case 12: // Ctrl-L (Clear screen and redraw line)
		ed.writeBytes([]byte("\x1b[2J\x1b[H"))
		ed.renderedLen = 0
		ed.renderedPos = 0
		ed.redraw()

	default:
		if (ch >= 32 && ch <= 126) || ch == '\t' {
			if ch == '\t' {
				ch = ' '
			}
			if ed.pos == len(ed.buf) {
				ed.buf = append(ed.buf, ch)
				ed.pos++
				ed.writeBytes([]byte{ch})
				ed.renderedLen = len(ed.buf)
				ed.renderedPos = ed.pos
			} else {
				ed.buf = append(ed.buf, 0)
				copy(ed.buf[ed.pos+1:], ed.buf[ed.pos:len(ed.buf)-1])
				ed.buf[ed.pos] = ch
				ed.pos++
				ed.redraw()
			}
		}
	}
	return ReadlineResult{Action: RlActionNone}
}

func ReadlineInkeyRoutine(inkey chan byte) {
	defer func() {
		if r := recover(); r != nil {
			Logf("ReadlineInkeyRoutine: recovers panic: %v", r)
		}
	}()

	ed := NewReadlineEditor(os.Stdout, *PASS_CTRL_C)

	rawCh := make(chan byte, 32)
	go func() {
		var bb [1]byte
		for {
			_, err := os.Stdin.Read(bb[:])
			if err != nil {
				return
			}
			rawCh <- bb[0]
		}
	}()

	var escBuf []byte
	escActive := false
	escTimer := time.NewTimer(0)
	if !escTimer.Stop() {
		<-escTimer.C
	}

	dispatchResult := func(res ReadlineResult) {
		switch res.Action {
		case RlActionQuit:
			if runtime.GOOS != "windows" {
				RestoreSttyState()
			}
			CloseActiveWire()
			ed.writeBytes([]byte("\r\n[tether: quit with ^]]\r\n"))
			os.Exit(0)
		case RlActionRaw:
			if res.RawByte == 3 {
				lineEchoSuppressor.Expect([]byte("^C\r\n"), 2*time.Second)
			} else {
				lineEchoSuppressor.Clear()
			}
			inkey <- res.RawByte
		case RlActionUCmd:
			uCmdChan <- res.Line
		case RlActionLine:
			expected := make([]byte, 0, len(res.Line)+2)
			expected = append(expected, []byte(res.Line)...)
			expected = append(expected, '\r', '\n')
			lineEchoSuppressor.Expect(expected, 2*time.Second)
			for _, b := range []byte(res.Line) {
				inkey <- b
				time.Sleep(3 * time.Millisecond)
			}
			inkey <- 13
		}
	}

	for {
		var ch byte
		if escActive {
			select {
			case ch = <-rawCh:
			case <-escTimer.C:
				escActive = false
				escBuf = nil
				continue
			}
		} else {
			ch = <-rawCh
		}

		if escActive {
			escBuf = append(escBuf, ch)
			if len(escBuf) == 1 {
				if ch == '[' || ch == 'O' {
					escTimer.Reset(100 * time.Millisecond)
					continue
				}
				// Alt-<key> (e.g. ESC b, ESC f, ESC d, ESC DEL)
				seq := string(escBuf)
				escActive = false
				escBuf = nil
				if ch == 8 || ch == 127 {
					ed.killWordBackward()
					continue
				}
				dispatchResult(ed.HandleEscape(seq))
				continue
			}
			// CSI / SS3 sequence ends on ASCII letter or '~'
			if (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '~' {
				seq := string(escBuf)
				escActive = false
				escBuf = nil
				dispatchResult(ed.HandleEscape(seq))
				continue
			}
			if len(escBuf) > 12 {
				escActive = false
				escBuf = nil
			}
			continue
		}

		if ch == 27 {
			escActive = true
			escBuf = escBuf[:0]
			escTimer.Reset(100 * time.Millisecond)
			continue
		}

		dispatchResult(ed.HandleByte(ch))
	}
}
