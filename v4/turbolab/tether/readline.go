package main

import (
	"fmt"
	"io"
	"os"
	"strings"
)

type LineReader struct {
	In      io.Reader
	Out     io.Writer
	History []string
}

func NewLineReader(in io.Reader, out io.Writer) *LineReader {
	if in == nil {
		in = os.Stdin
	}
	if out == nil {
		out = os.Stdout
	}
	return &LineReader{
		In:      in,
		Out:     out,
		History: make([]string, 0, 100),
	}
}

// ReadLine reads a line interactively in cbreak mode with line editing and history.
// Returns the completed line (with "\r\n" or "\n" trimmed), or "\x03" if ^C was typed.
func (lr *LineReader) ReadLine() (string, error) {
	var line []rune
	cursor := 0
	historyIdx := len(lr.History)

	redraw := func() {
		// Move cursor to col 0, clear line, print line, move cursor to pos
		fmt.Fprintf(lr.Out, "\r\033[K%s", string(line))
		if cursor < len(line) {
			diff := len(line) - cursor
			fmt.Fprintf(lr.Out, "\033[%dD", diff)
		}
	}

	buf := make([]byte, 16)
	for {
		n, err := lr.In.Read(buf)
		if err != nil {
			return "", err
		}
		if n == 0 {
			continue
		}

		for i := 0; i < n; i++ {
			b := buf[i]

			// Check for escape sequences (e.g. arrow keys: ESC [ A/B/C/D)
			if b == 0x1B && i+2 < n && buf[i+1] == '[' {
				code := buf[i+2]
				i += 2
				switch code {
				case 'A': // Up arrow -> older history
					if historyIdx > 0 {
						historyIdx--
						line = []rune(lr.History[historyIdx])
						cursor = len(line)
						redraw()
					}
				case 'B': // Down arrow -> newer history
					if historyIdx < len(lr.History)-1 {
						historyIdx++
						line = []rune(lr.History[historyIdx])
						cursor = len(line)
						redraw()
					} else if historyIdx == len(lr.History)-1 {
						historyIdx = len(lr.History)
						line = nil
						cursor = 0
						redraw()
					}
				case 'C': // Right arrow
					if cursor < len(line) {
						fmt.Fprintf(lr.Out, "%c", line[cursor])
						cursor++
					}
				case 'D': // Left arrow
					if cursor > 0 {
						cursor--
						fmt.Fprintf(lr.Out, "\b")
					}
				}
				continue
			}

			switch b {
			case 0x03: // Ctrl-C
				fmt.Fprintf(lr.Out, "^C\r\n")
				return "\x03", nil

			case 0x01: // Ctrl-A: beginning of line
				cursor = 0
				redraw()

			case 0x05: // Ctrl-E: end of line
				cursor = len(line)
				redraw()

			case 0x08, 0x7F: // Backspace
				if cursor > 0 {
					line = append(line[:cursor-1], line[cursor:]...)
					cursor--
					redraw()
				}

			case '\r', '\n': // Enter
				fmt.Fprintf(lr.Out, "\r\n")
				res := string(line)
				trimmed := strings.TrimSpace(res)
				if trimmed == "^C" || trimmed == "^c" {
					return "\x03", nil
				}
				if len(trimmed) > 0 {
					lr.History = append(lr.History, res)
				}
				return res, nil

			default:
				if b >= 32 && b <= 126 {
					r := rune(b)
					if cursor == len(line) {
						line = append(line, r)
						cursor++
						fmt.Fprintf(lr.Out, "%c", r)
					} else {
						line = append(line[:cursor], append([]rune{r}, line[cursor:]...)...)
						cursor++
						redraw()
					}
				}
			}
		}
	}
}
