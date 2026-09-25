package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

// WatchpointConfig contains resolved watchpoint parameters sent to firmware.
type WatchpointConfig struct {
	Type  byte // 'r', 'w', 'x' or 0
	Addr  uint16
	Count uint32
}

// WatchpointSpec holds an unresolved watchpoint specification parsed from command-line flags.
type WatchpointSpec struct {
	Type     byte   // 'r', 'w', 'x'
	AddrExpr string // e.g. "0x1234", "$1234", "1234", "@kernel+0x10", "@kernel"
	Count    uint32
}

// Resolve resolves the watchpoint address against scanned OS-9 modules.
func (w *WatchpointSpec) Resolve(scannedMods []*ScannedModuleInfo) (WatchpointConfig, error) {
	addr, err := resolveAddress(w.AddrExpr, scannedMods)
	if err != nil {
		return WatchpointConfig{}, err
	}
	return WatchpointConfig{
		Type:  w.Type,
		Addr:  addr,
		Count: w.Count,
	}, nil
}

// parseWatchpointSpec parses a watchpoint expression into a WatchpointSpec.
// Formats supported:
//   - r:<addr>[:<count>]
//   - w:<addr>[:<count>]
//   - x:<addr>[:<count>]
//   - @<module>[+<offset>][:<count>] (defaults to execution 'x')
//
// Returns (spec, isWatchpoint, error).
func parseWatchpointSpec(s string) (*WatchpointSpec, bool, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil, false, nil
	}

	// Shorthand: @modulename[+offset][:count]
	if strings.HasPrefix(s, "@") {
		parts := strings.Split(s, ":")
		if len(parts) > 2 {
			return nil, true, fmt.Errorf("invalid watchpoint format %q (expected @module[+offset][:count])", s)
		}
		addrExpr := parts[0]
		count := uint32(1)
		if len(parts) == 2 {
			c, err := parseCount(parts[1])
			if err != nil {
				return nil, true, fmt.Errorf("invalid watchpoint count %q in %q: %w", parts[1], s, err)
			}
			if c == 0 {
				return nil, true, fmt.Errorf("watchpoint count must be >= 1 in %q", s)
			}
			count = uint32(c)
		}
		return &WatchpointSpec{
			Type:     'x',
			AddrExpr: addrExpr,
			Count:    count,
		}, true, nil
	}

	// Standard: r:<addr>[:count], w:<addr>[:count], x:<addr>[:count]
	parts := strings.Split(s, ":")
	if len(parts) < 2 || len(parts) > 3 {
		return nil, false, nil
	}
	t := strings.ToLower(parts[0])
	if t != "r" && t != "w" && t != "x" {
		return nil, false, nil
	}
	addrExpr := strings.TrimSpace(parts[1])
	if addrExpr == "" {
		return nil, true, fmt.Errorf("missing address in watchpoint %q", s)
	}

	count := uint32(1)
	if len(parts) == 3 {
		c, err := parseCount(parts[2])
		if err != nil {
			return nil, true, fmt.Errorf("invalid watchpoint count %q in %q: %w", parts[2], s, err)
		}
		if c == 0 {
			return nil, true, fmt.Errorf("watchpoint count must be >= 1 in %q", s)
		}
		count = uint32(c)
	}

	return &WatchpointSpec{
		Type:     t[0],
		AddrExpr: addrExpr,
		Count:    count,
	}, true, nil
}

// resolveAddress resolves an address expression (numeric literal or @module+offset) to a uint16.
func resolveAddress(addrExpr string, scannedMods []*ScannedModuleInfo) (uint16, error) {
	addrExpr = strings.TrimSpace(addrExpr)
	if strings.HasPrefix(addrExpr, "@") {
		return parseModuleAddress(addrExpr[1:], scannedMods)
	}
	return parseAddressLiteral(addrExpr)
}

// parseAddressLiteral parses numeric address literals ($hex, 0xhex, hex, or decimal).
func parseAddressLiteral(s string) (uint16, error) {
	s = strings.TrimSpace(s)
	if strings.HasPrefix(s, "$") {
		v, err := strconv.ParseUint(s[1:], 16, 16)
		return uint16(v), err
	}
	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		v, err := strconv.ParseUint(s[2:], 16, 16)
		return uint16(v), err
	}
	v, err := strconv.ParseUint(s, 16, 16)
	if err == nil {
		return uint16(v), nil
	}
	v, err = strconv.ParseUint(s, 10, 16)
	return uint16(v), err
}

// parseOffset parses an integer offset that can be decimal or hexadecimal:
// - Hex with '$' prefix: e.g. "$10", "$FE"
// - Hex with '0x' or '0X' prefix: e.g. "0x10", "0xfe"
// - Hex containing hex letters [a-fA-F]: e.g. "1a", "fe"
// - Decimal digits: e.g. "16", "100", "0"
func parseOffset(s string) (uint16, error) {
	s = strings.TrimSpace(s)
	if s == "" || s == "0" {
		return 0, nil
	}
	if strings.HasPrefix(s, "$") {
		v, err := strconv.ParseUint(s[1:], 16, 16)
		return uint16(v), err
	}
	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		v, err := strconv.ParseUint(s[2:], 16, 16)
		return uint16(v), err
	}
	hasHexLetter := false
	for _, c := range s {
		if (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') {
			hasHexLetter = true
			break
		}
	}
	if hasHexLetter {
		v, err := strconv.ParseUint(s, 16, 16)
		return uint16(v), err
	}
	v, err := strconv.ParseUint(s, 10, 16)
	if err == nil {
		return uint16(v), nil
	}
	v, err = strconv.ParseUint(s, 16, 16)
	return uint16(v), err
}

// parseModuleAddress resolves a module-relative address like "kernel+0x10" or "ioman+16"
// against the scanned primordial OS-9 modules.
func parseModuleAddress(expr string, scannedMods []*ScannedModuleInfo) (uint16, error) {
	if len(scannedMods) == 0 {
		return 0, fmt.Errorf("no OS-9 modules loaded or detected")
	}

	expr = strings.TrimSpace(expr)
	idx := strings.IndexAny(expr, "+-")
	var modName, offsetStr string
	sign := byte('+')
	if idx >= 0 {
		modName = strings.TrimSpace(expr[:idx])
		sign = expr[idx]
		offsetStr = strings.TrimSpace(expr[idx+1:])
	} else {
		modName = expr
		offsetStr = "0"
	}

	if modName == "" {
		return 0, fmt.Errorf("missing module name in @%s", expr)
	}

	// Clean up module name (e.g. if someone wrote kernel.list)
	if strings.HasSuffix(strings.ToLower(modName), ".list") {
		modName = modName[:len(modName)-5]
	}

	offset, err := parseOffset(offsetStr)
	if err != nil {
		return 0, fmt.Errorf("invalid offset %q in @%s: %w", offsetStr, expr, err)
	}

	var matched *ScannedModuleInfo
	for _, m := range scannedMods {
		if strings.EqualFold(m.Name, modName) || strings.EqualFold(m.FullName, modName) {
			matched = m
			break
		}
	}
	if matched == nil {
		// Try prefix match on FullName (e.g. modName is "kernel" and FullName is "kernel.0d4eec829c")
		prefix := strings.ToLower(modName) + "."
		for _, m := range scannedMods {
			if strings.HasPrefix(strings.ToLower(m.FullName), prefix) {
				matched = m
				break
			}
		}
	}

	if matched == nil {
		var names []string
		for _, m := range scannedMods {
			names = append(names, m.Name)
		}
		return 0, fmt.Errorf("module %q not found in loaded/detected OS-9 modules (available: %s)",
			modName, strings.Join(names, ", "))
	}

	if offset > matched.Size && matched.Size > 0 {
		fmt.Fprintf(os.Stderr, "Warning: offset +$%04X exceeds size of module %q ($%04X)\n",
			offset, matched.Name, matched.Size)
	}

	var addr uint16
	if sign == '+' {
		sum := uint32(matched.BaseAddr) + uint32(offset)
		if sum > 0xFFFF {
			return 0, fmt.Errorf("calculated address for @%s ($%05X) exceeds 64KB address space", expr, sum)
		}
		addr = uint16(sum)
	} else {
		if offset > matched.BaseAddr {
			return 0, fmt.Errorf("calculated address for @%s is negative ($%04X - $%04X)", expr, matched.BaseAddr, offset)
		}
		addr = matched.BaseAddr - offset
	}

	return addr, nil
}

// parseWatchList parses a comma-separated list of watch expressions for --watch:
// e.g. "0x0020,0x0021,@kernel+0x1B,w:0x0030"
// Each item defaults to type 0 (any cycle: r, w, x) unless prefixed with r:, w:, or x:.
func parseWatchList(s string) ([]*WatchpointSpec, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil, nil
	}
	parts := strings.Split(s, ",")
	var specs []*WatchpointSpec
	for _, item := range parts {
		item = strings.TrimSpace(item)
		if item == "" {
			continue
		}
		var wpType byte = 0 // 0 = any cycle (r, w, x)
		addrExpr := item
		count := uint32(0) // 0 = every time

		low := strings.ToLower(item)
		if strings.HasPrefix(low, "r:") || strings.HasPrefix(low, "w:") || strings.HasPrefix(low, "x:") {
			wpType = low[0]
			addrExpr = strings.TrimSpace(item[2:])
		}

		if strings.Contains(addrExpr, ":") {
			sub := strings.Split(addrExpr, ":")
			if len(sub) == 2 {
				addrExpr = strings.TrimSpace(sub[0])
				c, err := parseCount(sub[1])
				if err != nil {
					return nil, fmt.Errorf("invalid count %q in watchpoint %q: %w", sub[1], item, err)
				}
				count = uint32(c)
			} else {
				return nil, fmt.Errorf("invalid watchpoint format %q", item)
			}
		}

		if addrExpr == "" {
			return nil, fmt.Errorf("empty address in watchpoint %q", item)
		}

		specs = append(specs, &WatchpointSpec{
			Type:     wpType,
			AddrExpr: addrExpr,
			Count:    count,
		})
	}
	return specs, nil
}
