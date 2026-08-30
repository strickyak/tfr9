package main

// TFR911 compatibility: C_CYCLE handler for uncompressed cycle tracing.
//
// The TFR911 firmware sends C_CYCLE (200) packets with 8-byte payloads
// containing cycle number, flags (including FIC/First Instruction Cycle),
// kind, data, and address. This format is distinct from Centipede's
// C_COMPRESSED_CYCLES, which lacks the FIC marking that the HD63C09EP's
// LIC pin provides.

import "fmt"

// CY_FIC = 7 is defined in tconsole.go's iota block.

var Seen [0x10000]bool
var LastOpAddr uint

// HandleCCycle processes a C_CYCLE packet from the TFR911 firmware.
// The packet payload is 8 bytes:
//   [cy3 cy2 cy1 cy0] [flags_kind] [data] [addr_hi addr_lo]
// where flags_kind = (kind << 5) | (flags & 31)
func HandleCCycle(pack []byte, person Personality, channelToPico chan []byte) {
	if len(pack) != 8 {
		return
	}
	const GLOSS = true

	_cy := (uint(pack[0]) << 24) + (uint(pack[1]) << 16) + (uint(pack[2]) << 8) + uint(pack[3])
	_fl := pack[4] & 31
	_kind := pack[4] >> 5
	_data := pack[5]
	_addr := (uint(pack[6]) << 8) + uint(pack[7])

	Cycle++

	if _addr == 0xFFFF && _kind == CY_READ {
		_kind = CY_IDLE
	}

	if _kind == CY_IDLE {
		Logf("cy - ---- -- %s#%02x#%d", LookupCpuFlags[_fl], _fl, _cy)
		return
	}

	var s string

	if person.HasMMap() {
		s = Format("cy %s %04x %02x %s#%02x#%d", CycleKindStr[_kind], _addr, _data, LookupCpuFlags[_fl], _fl, _cy)
		phys := the_ram.Physical(uint(_addr))
		if _kind == CY_FIC {
			if GLOSS {
				GlossFirstCycle(_addr, _data)
			}
			modName, modOffset := person.MemoryModuleOf(phys)
			mmap := person.CurrentHardwareMMap()
			Logf("%s %s%%%06x :%q+%04x %s", s, mmap, phys, modName, modOffset, AsmSourceLine(modName, modOffset))
		} else {
			g := ""
			if GLOSS {
				g = GlossLaterCycle(_addr, _data)
			}
			Logf("%s %%%06x%s", s, phys, g)
		}
	} else {
		if _kind == CY_WRITE {
			the_ram.Poke1(_addr, _data) // for Coco2 Framebuffer
		}

		if _kind == CY_FIC {
			if Seen[_addr] {
				_kind = CY_SEEN_OP
			} else {
				_kind = CY_UNSEEN_OP
				Seen[_addr] = true
			}
			LastOpAddr = _addr
			s = Format("cy %s %04x %02x %s#%02x#%d", CycleKindStr[_kind], _addr, _data, LookupCpuFlags[_fl], _fl, _cy)
			if GLOSS {
				GlossFirstCycle(_addr, _data)
			}
			modName, modOffset := person.MemoryModuleOf(_addr)
			Logf("%s :%q+%04x %s", s, modName, modOffset, AsmSourceLine(modName, modOffset))
		} else {
			if _kind == CY_READ && LastOpAddr+1 == _addr {
				_kind = CY_MORE
				LastOpAddr = _addr
			}
			s = Format("cy %s %04x %02x %s#%02x#%d", CycleKindStr[_kind], _addr, _data, LookupCpuFlags[_fl], _fl, _cy)
			g := ""
			if GLOSS {
				g = GlossLaterCycle(_addr, _data)
			}
			Logf("%s %s", s, g)
		}
	}
	_ = fmt.Sprintf // ensure fmt is used
}
