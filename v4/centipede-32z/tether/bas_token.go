package main

import (
	"bytes"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"
)

// Token definitions for Color BASIC, Extended Color BASIC, Disk Basic,
// and CoCo 3 Super Extended Color BASIC.
var primaryTokens = map[string]byte{
	// Color BASIC ($80 - $B4)
	"FOR":     0x80,
	"GO":      0x81,
	"REM":     0x82,
	"'":       0x83,
	"ELSE":    0x84,
	"IF":      0x85,
	"DATA":    0x86,
	"PRINT":   0x87,
	"ON":      0x88,
	"INPUT":   0x89,
	"END":     0x8A,
	"NEXT":    0x8B,
	"DIM":     0x8C,
	"READ":    0x8D,
	"RUN":     0x8E,
	"RESTORE": 0x8F,
	"RETURN":  0x90,
	"STOP":    0x91,
	"POKE":    0x92,
	"CONT":    0x93,
	"LIST":    0x94,
	"CLEAR":   0x95,
	"NEW":     0x96,
	"CLOAD":   0x97,
	"CSAVE":   0x98,
	"OPEN":    0x99,
	"CLOSE":   0x9A,
	"LLIST":   0x9B,
	"SET":     0x9C,
	"RESET":   0x9D,
	"CLS":     0x9E,
	"MOTOR":   0x9F,
	"SOUND":   0xA0,
	"AUDIO":   0xA1,
	"EXEC":    0xA2,
	"SKIPF":   0xA3,
	"TAB(":    0xA4,
	"TO":      0xA5,
	"SUB":     0xA6,
	"THEN":    0xA7,
	"NOT":     0xA8,
	"STEP":    0xA9,
	"OFF":     0xAA,
	"+":       0xAB,
	"-":       0xAC,
	"*":       0xAD,
	"/":       0xAE,
	"^":       0xAF,
	"AND":     0xB0,
	"OR":      0xB1,
	">":       0xB2,
	"=":       0xB3,
	"<":       0xB4,

	// Extended Color BASIC ($B5 - $CD)
	"DEL":    0xB5,
	"EDIT":   0xB6,
	"TRON":   0xB7,
	"TROFF":  0xB8,
	"DEF":    0xB9,
	"LET":    0xBA,
	"LINE":   0xBB,
	"PCLS":   0xBC,
	"PSET":   0xBD,
	"PRESET": 0xBE,
	"SCREEN": 0xBF,
	"PCLEAR": 0xC0,
	"COLOR":  0xC1,
	"CIRCLE": 0xC2,
	"PAINT":  0xC3,
	"GET":    0xC4,
	"PUT":    0xC5,
	"DRAW":   0xC6,
	"PCOPY":  0xC7,
	"PMODE":  0xC8,
	"PLAY":   0xC9,
	"DLOAD":  0xCA,
	"RENUM":  0xCB,
	"FN":     0xCC,
	"USING":  0xCD,

	// Disk Extended Color BASIC ($CE - $E1)
	"DIR":    0xCE,
	"DRIVE":  0xCF,
	"FIELD":  0xD0,
	"FILES":  0xD1,
	"KILL":   0xD2,
	"LOAD":   0xD3,
	"LSET":   0xD4,
	"MERGE":  0xD5,
	"RENAME": 0xD6,
	"RSET":   0xD7,
	"SAVE":   0xD8,
	"WRITE":  0xD9,
	"VERIFY": 0xDA,
	"UNLOAD": 0xDB,
	"DSKINI": 0xDC,
	"BACKUP": 0xDD,
	"COPY":   0xDE,
	"DSKI$":  0xDF,
	"DSKO$":  0xE0,
	"DOS":    0xE1,

	// Super Extended Color BASIC / CoCo 3 ($E2 - $F8)
	"WIDTH":   0xE2,
	"PALETTE": 0xE3,
	"HSCREEN": 0xE4,
	"LPOKE":   0xE5,
	"HCLS":    0xE6,
	"HCOLOR":  0xE7,
	"HPAINT":  0xE8,
	"HCIRCLE": 0xE9,
	"HLINE":   0xEA,
	"HGET":    0xEB,
	"HPUT":    0xEC,
	"HBUFF":   0xED,
	"HPRINT":  0xEE,
	"ERR":     0xEF,
	"BRK":     0xF0,
	"LOCATE":  0xF1,
	"HSTAT":   0xF2,
	"HSET":    0xF3,
	"HRESET":  0xF4,
	"HDRAW":   0xF5,
	"CMP":     0xF6,
	"RGB":     0xF7,
	"ATTR":    0xF8,
}

var secondaryTokens = map[string]byte{
	// Color BASIC secondary functions ($80 - $93)
	"SGN":    0x80,
	"INT":    0x81,
	"ABS":    0x82,
	"USR":    0x83,
	"RND":    0x84,
	"SIN":    0x85,
	"PEEK":   0x86,
	"LEN":    0x87,
	"STR$":   0x88,
	"VAL":    0x89,
	"ASC":    0x8A,
	"CHR$":   0x8B,
	"EOF":    0x8C,
	"JOYSTK": 0x8D,
	"LEFT$":  0x8E,
	"RIGHT$": 0x8F,
	"MID$":   0x90,
	"POINT":  0x91,
	"INKEY$": 0x92,
	"MEM":    0x93,

	// Extended Color BASIC secondary functions ($94 - $A1)
	"ATN":     0x94,
	"COS":     0x95,
	"TAN":     0x96,
	"EXP":     0x97,
	"FIX":     0x98,
	"LOG":     0x99,
	"POS":     0x9A,
	"SQR":     0x9B,
	"HEX$":    0x9C,
	"VARPTR":  0x9D,
	"INSTR":   0x9E,
	"TIMER":   0x9F,
	"PPOINT":  0xA0,
	"STRING$": 0xA1,

	// Disk Extended Color BASIC secondary functions ($A2 - $A7)
	"CVN":  0xA2,
	"FREE": 0xA3,
	"LOC":  0xA4,
	"LOF":  0xA5,
	"MKN$": 0xA6,
	"AS":   0xA7,

	// Super Extended Color BASIC / CoCo 3 secondary functions ($A8 - $AC)
	"LPEEK":  0xA8,
	"BUTTON": 0xA9,
	"HPOINT": 0xAA,
	"ERNO":   0xAB,
	"ERLIN":  0xAC,
}

type tokenRule struct {
	keyword string
	bytes   []byte
}

var tokenRules []tokenRule
var primaryLookup = map[byte]string{}
var secondaryLookup = map[byte]string{}

func init() {
	for k, v := range primaryTokens {
		tokenRules = append(tokenRules, tokenRule{keyword: k, bytes: []byte{v}})
		primaryLookup[v] = k
	}
	for k, v := range secondaryTokens {
		tokenRules = append(tokenRules, tokenRule{keyword: k, bytes: []byte{0xFF, v}})
		secondaryLookup[v] = k
	}
	// Sort keywords by length descending so longer keywords match before substrings (e.g. HSCREEN before SCREEN)
	sort.Slice(tokenRules, func(i, j int) bool {
		return len(tokenRules[i].keyword) > len(tokenRules[j].keyword)
	})
}

// TokenizeLine tokenizes a single line of BASIC text (without line number).
func TokenizeLine(s string) ([]byte, error) {
	var res []byte
	i := 0
	inQuote := false
	inRem := false
	inData := false

	for i < len(s) {
		c := s[i]

		if inQuote {
			res = append(res, c)
			if c == '"' {
				inQuote = false
			}
			i++
			continue
		}

		if inRem {
			res = append(res, c)
			i++
			continue
		}

		if inData {
			if c == ':' {
				inData = false
				res = append(res, c)
				i++
				continue
			}
			res = append(res, c)
			i++
			continue
		}

		if c == '"' {
			inQuote = true
			res = append(res, c)
			i++
			continue
		}

		if c == '?' {
			// '?' is shortcut for PRINT ($87)
			res = append(res, 0x87)
			i++
			continue
		}

		if c == '\'' {
			// Single quote is token $83 and starts a comment (like REM)
			res = append(res, 0x83)
			inRem = true
			i++
			continue
		}

		// Match keywords (case-insensitive)
		matched := false
		upperRemain := strings.ToUpper(s[i:])
		for _, rule := range tokenRules {
			if strings.HasPrefix(upperRemain, rule.keyword) {
				res = append(res, rule.bytes...)
				i += len(rule.keyword)
				matched = true
				if rule.keyword == "REM" {
					inRem = true
				} else if rule.keyword == "DATA" {
					inData = true
				}
				break
			}
		}

		if !matched {
			res = append(res, c)
			i++
		}
	}

	return res, nil
}

// DetokenizeLine detokenizes a line from tokens back to ASCII text.
func DetokenizeLine(tokens []byte) string {
	var buf strings.Builder
	for i := 0; i < len(tokens); i++ {
		b := tokens[i]
		if b == 0xFF && i+1 < len(tokens) {
			i++
			sec := tokens[i]
			if name, ok := secondaryLookup[sec]; ok {
				buf.WriteString(name)
			} else {
				buf.WriteString(fmt.Sprintf("{FF:%02X}", sec))
			}
		} else if b >= 0x80 {
			if name, ok := primaryLookup[b]; ok {
				buf.WriteString(name)
			} else {
				buf.WriteString(fmt.Sprintf("{%02X}", b))
			}
		} else {
			buf.WriteByte(b)
		}
	}
	return buf.String()
}

// BasicLine represents one line of a BASIC program.
type BasicLine struct {
	LineNum uint16
	Tokens  []byte
}

// ParseAsciiBasic parses an ASCII BASIC program into structured lines.
func ParseAsciiBasic(content string) ([]BasicLine, error) {
	var lines []BasicLine
	rawLines := strings.Split(content, "\n")

	for lineIdx, raw := range rawLines {
		line := strings.TrimRight(raw, "\r\n")
		// Strip leading ~~ escape if present (common when pasted into Tether)
		if strings.HasPrefix(line, "~~") {
			line = line[2:]
		} else if strings.HasPrefix(line, "~") && len(line) > 1 && line[1] >= '0' && line[1] <= '9' {
			line = line[1:]
		}
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		// Find leading line number
		k := 0
		for k < len(line) && line[k] >= '0' && line[k] <= '9' {
			k++
		}
		if k == 0 {
			return nil, fmt.Errorf("line %d has no line number: %q", lineIdx+1, line)
		}

		lineNumVal, err := strconv.ParseUint(line[:k], 10, 16)
		if err != nil {
			return nil, fmt.Errorf("invalid line number %q on line %d: %v", line[:k], lineIdx+1, err)
		}

		statementText := strings.TrimLeft(line[k:], " \t")
		tokBytes, err := TokenizeLine(statementText)
		if err != nil {
			return nil, fmt.Errorf("error tokenizing line %d (%d): %v", lineIdx+1, lineNumVal, err)
		}

		lines = append(lines, BasicLine{
			LineNum: uint16(lineNumVal),
			Tokens:  tokBytes,
		})
	}

	if len(lines) == 0 {
		return nil, fmt.Errorf("no BASIC lines found in file")
	}

	// Sort lines by line number
	sort.Slice(lines, func(i, j int) bool {
		return lines[i].LineNum < lines[j].LineNum
	})

	return lines, nil
}

// ParseTokenizedBasic parses a keyword-substituted (.BAS) binary file into BasicLines.
func ParseTokenizedBasic(data []byte) ([]BasicLine, error) {
	// If it has Disk BASIC preamble: byte 0 is 0xFF, followed by 2-byte length
	if len(data) >= 3 && data[0] == 0xFF {
		data = data[3:]
	}

	var lines []BasicLine
	offset := 0

	for offset < len(data) {
		// Need at least 2 bytes link pointer
		if offset+2 > len(data) {
			break
		}
		link := (uint(data[offset]) << 8) | uint(data[offset+1])
		if link == 0 {
			// End of program marker
			break
		}
		if offset+4 > len(data) {
			return nil, fmt.Errorf("truncated BASIC line header at offset %d", offset)
		}
		lineNum := (uint16(data[offset+2]) << 8) | uint16(data[offset+3])
		offset += 4

		// Scan until 0x00 terminating the line
		startText := offset
		for offset < len(data) && data[offset] != 0 {
			offset++
		}
		if offset >= len(data) {
			return nil, fmt.Errorf("unterminated BASIC line %d", lineNum)
		}
		lineTokens := data[startText:offset]
		offset++ // skip 0x00

		lines = append(lines, BasicLine{
			LineNum: lineNum,
			Tokens:  lineTokens,
		})
	}

	if len(lines) == 0 {
		return nil, fmt.Errorf("no lines parsed from tokenized BASIC file")
	}

	return lines, nil
}

// IsTokenizedBasic checks if data is tokenized binary (.BAS) or ASCII text.
func IsTokenizedBasic(data []byte) bool {
	if len(data) >= 3 && data[0] == 0xFF {
		fileLen := (int(data[1]) << 8) | int(data[2])
		if fileLen > 0 && fileLen <= len(data) {
			return true
		}
	}
	// Check for non-ASCII bytes (>= 0x80)
	for _, b := range data {
		if b >= 0x80 {
			return true
		}
	}
	return false
}

// CompileBasicProgram lays out BasicLines starting at txttab with proper link pointers
// and returns the raw bytes and new end address (VARTAB).
func CompileBasicProgram(lines []BasicLine, txttab uint) (programBytes []byte, newEnd uint) {
	var buf bytes.Buffer
	curAddr := txttab

	for _, line := range lines {
		// Line length: 2 bytes link + 2 bytes line number + tokens + 1 byte null terminator
		lineLen := uint(4 + len(line.Tokens) + 1)
		nextAddr := curAddr + lineLen

		buf.WriteByte(byte(nextAddr >> 8))
		buf.WriteByte(byte(nextAddr & 0xFF))
		buf.WriteByte(byte(line.LineNum >> 8))
		buf.WriteByte(byte(line.LineNum & 0xFF))
		buf.Write(line.Tokens)
		buf.WriteByte(0x00)

		curAddr = nextAddr
	}

	// End of program: 2 null bytes (link pointer = 0x0000)
	buf.WriteByte(0x00)
	buf.WriteByte(0x00)

	programBytes = buf.Bytes()
	newEnd = curAddr + 2
	return programBytes, newEnd
}

// LoadBasicFile reads filename, parses either ASCII or tokenized BASIC, and compiles
// it for loading at txttab.
func LoadBasicFile(filename string, txttab uint) (programBytes []byte, newEnd uint, numLines int, err error) {
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, 0, 0, err
	}

	var lines []BasicLine
	if IsTokenizedBasic(data) {
		lines, err = ParseTokenizedBasic(data)
		if err != nil {
			return nil, 0, 0, fmt.Errorf("parse tokenized BASIC error: %v", err)
		}
	} else {
		lines, err = ParseAsciiBasic(string(data))
		if err != nil {
			return nil, 0, 0, fmt.Errorf("parse ASCII BASIC error: %v", err)
		}
	}

	programBytes, newEnd = CompileBasicProgram(lines, txttab)
	return programBytes, newEnd, len(lines), nil
}
