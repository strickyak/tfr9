package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"
)

type ScannedModuleInfo struct {
	Name     string
	FullName string // "%s.%04x%06x"
	BaseAddr uint16
	Size     uint16
	Crc      uint32
	CrcHex   string
}

// ScanImageForOs9Modules scans a 64KB memory image for all valid primordial OS-9 modules.
func ScanImageForOs9Modules(ram []byte) []*ScannedModuleInfo {
	var modules []*ScannedModuleInfo
	limit := len(ram) - 9
	if limit > 0xFEF8 {
		limit = 0xFEF8
	}

	for i := 0; i < limit; i++ {
		if ram[i] == 0x87 && ram[i+1] == 0xCD {
			size := binary.BigEndian.Uint16(ram[i+2 : i+4])
			if size < 9 || int(i)+int(size) > len(ram) {
				continue
			}
			namoff := binary.BigEndian.Uint16(ram[i+4 : i+6])
			if namoff >= size {
				continue
			}

			// Verify header parity: XOR sum of first 9 bytes must be 0xFF
			var chk byte
			for j := 0; j < 9; j++ {
				chk ^= ram[i+j]
			}
			if chk != 0xFF {
				continue
			}

			// Extract module name
			var nameBuf []byte
			for p := int(i) + int(namoff); p < int(i)+int(size); p++ {
				b := ram[p]
				ch := b & 0x7F
				if ch < 32 || ch > 126 {
					nameBuf = nil
					break
				}
				nameBuf = append(nameBuf, ch)
				if (b & 0x80) != 0 {
					break
				}
			}
			if len(nameBuf) == 0 {
				continue
			}
			modName := string(nameBuf)

			// Extract 3-byte CRC at end of module
			c1 := ram[int(i)+int(size)-3]
			c2 := ram[int(i)+int(size)-2]
			c3 := ram[int(i)+int(size)-1]
			crcVal := (uint32(c1) << 16) | (uint32(c2) << 8) | uint32(c3)
			crcHex := fmt.Sprintf("%02X%02X%02X", c1, c2, c3)
			fullName := fmt.Sprintf("%s.%04x%06x", strings.ToLower(modName), size, crcVal)

			modules = append(modules, &ScannedModuleInfo{
				Name:     modName,
				FullName: fullName,
				BaseAddr: uint16(i),
				Size:     size,
				Crc:      crcVal,
				CrcHex:   crcHex,
			})
		}
	}

	return modules
}

type ListingModuleInfo struct {
	IsModule bool
	ModSize  uint16
	Crc      uint32
	CrcHex   string
}

// InspectListingModule checks whether a listing is an OS-9 module (has MOD at $0000 and EMOD).
func InspectListingModule(filename string) (*ListingModuleInfo, error) {
	fd, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer fd.Close()

	scanner := bufio.NewScanner(fd)
	var hasMod0000 bool
	var modSize uint16
	var hasEmod bool
	var crcHex string
	var crcVal uint32

	modPattern := regexp.MustCompile(`(?i)^0000\s+(87CD)([0-9A-Fa-f]{4})[0-9A-Fa-f]*\s+.*?\bmod\b`)
	emodPattern := regexp.MustCompile(`(?i)^[0-9A-Fa-f]{4}\s+([0-9A-Fa-f]{6})\s+.*?\bemod\b`)

	for scanner.Scan() {
		line := scanner.Text()
		if !hasMod0000 {
			m := modPattern.FindStringSubmatch(line)
			if m != nil {
				hasMod0000 = true
				sz, err := strconv.ParseUint(m[2], 16, 16)
				if err == nil {
					modSize = uint16(sz)
				}
			}
		}
		if !hasEmod {
			m := emodPattern.FindStringSubmatch(line)
			if m != nil {
				hasEmod = true
				crcHex = strings.ToUpper(m[1])
				val, err := strconv.ParseUint(crcHex, 16, 32)
				if err == nil {
					crcVal = uint32(val)
				}
			}
		}
	}

	if hasMod0000 && hasEmod {
		return &ListingModuleInfo{
			IsModule: true,
			ModSize:  modSize,
			Crc:      crcVal,
			CrcHex:   crcHex,
		}, nil
	}

	return &ListingModuleInfo{IsModule: false}, nil
}

type ListingSource struct {
	Src         map[uint16]string
	Filename    string
	IsOs9Module bool
	ModuleName  string
	FullName    string
	BaseAddr    uint16
	ModSize     uint16
}

var (
	parseLabel = regexp.MustCompile(`^([[:xdigit:]]{4})  +[(].*?[)]:[0-9]{5} +([A-Za-z0-9._$@]+[:]?) *$`)
	parseLine  = regexp.MustCompile(`^([[:xdigit:]]{4}) [[:xdigit:]]+ +([(].*?[)]:[0-9]{5})(.*)$`)
)

// LoadListingFile loads an assembly listing, adding baseAddr to each instruction address.
func LoadListingFile(filename string, baseAddr uint16) (*ListingSource, error) {
	fd, err := os.Open(filename)
	if err != nil {
		return nil, err
	}
	defer fd.Close()

	d := make(map[uint16]string)
	scanner := bufio.NewScanner(fd)

	var sawAddr, sawLabel string
	for scanner.Scan() {
		text := scanner.Text()
		m := parseLine.FindStringSubmatch(text)
		if m != nil {
			hexaddr := m[1]
			raw := m[3]
			trimmed := strings.TrimSpace(raw)
			addr, err := strconv.ParseUint(hexaddr, 16, 16)
			if err == nil && trimmed != "" {
				hasLabel := false
				var line string
				if sawLabel != "" && sawAddr == hexaddr {
					hasLabel = true
					line = sawLabel + " " + trimmed
				} else {
					if len(raw) > 9 && raw[9] != ' ' && raw[9] != '\t' {
						hasLabel = true
					}
					line = trimmed
				}
				sawAddr = ""
				sawLabel = ""

				if !hasLabel {
					line = "  " + line
				}
				actualAddr := baseAddr + uint16(addr)
				d[actualAddr] = line
			}
		}

		ml := parseLabel.FindStringSubmatch(text)
		if ml != nil {
			sawAddr = ml[1]
			sawLabel = ml[2]
		}
	}

	return &ListingSource{
		Src:      d,
		Filename: filename,
		BaseAddr: baseAddr,
	}, nil
}

// LoadAndRelocateListing inspects a listing file. If it is an OS-9 module, it matches it
// with scanned primordial modules in RAM to find its BaseAddr. If it is not found in RAM,
// it returns nil so low memory is not polluted. If it is an absolute listing, it is loaded at baseAddr 0.
func LoadAndRelocateListing(filename string, scannedMods []*ScannedModuleInfo) (*ListingSource, *ScannedModuleInfo, error) {
	info, err := InspectListingModule(filename)
	if err != nil {
		return nil, nil, err
	}

	if !info.IsModule {
		src, err := LoadListingFile(filename, 0)
		return src, nil, err
	}

	// Match by CRC
	var matched *ScannedModuleInfo
	for _, m := range scannedMods {
		if info.CrcHex != "" && m.CrcHex == info.CrcHex {
			matched = m
			break
		}
	}
	// Fallback match by size
	if matched == nil && info.ModSize > 0 {
		for _, m := range scannedMods {
			if m.Size == info.ModSize {
				matched = m
				break
			}
		}
	}

	if matched == nil {
		return nil, nil, nil // OS-9 module not present in RAM image
	}

	src, err := LoadListingFile(filename, matched.BaseAddr)
	if err != nil {
		return nil, nil, err
	}
	src.IsOs9Module = true
	src.ModuleName = matched.Name
	src.FullName = matched.FullName
	src.ModSize = matched.Size

	return src, matched, nil
}

type MultiListings []*ListingSource

func (ml MultiListings) Lookup(addr uint16) string {
	for _, l := range ml {
		if s, ok := l.Src[addr]; ok && s != "" {
			return s
		}
	}
	return ""
}
