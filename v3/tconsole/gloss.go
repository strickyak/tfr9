package main

var gloss uint
var cycle uint

func GlossFirstCycle(_ uint, d byte) string {
	cycle = 0
	gloss = uint(d)
	return ""
}

var ComputedGloss []string
var IndexedExtra uint

func GlossLaterCycle(_ uint, d byte) string {
	cycle++
	switch gloss {
	case 0x10: // prefix $10
		gloss = 0x1000 | uint(d)
	case 0x103F: // SWI2
		return GlossRegsSwi2[cycle]
	case 0x3B: // RTI
		return GlossRegsRti[cycle]
	case 0x34: // PSHS
		if cycle == 1 {
			ComputedGloss = ComputePshsGloss(d)
		} else {
			if cycle < uint(len(ComputedGloss)) {
				return ComputedGloss[cycle]
			}
		}
	case 0x35: // PULS
		if cycle == 1 {
			ComputedGloss = ComputePulsGloss(d)
		} else {
			if cycle < uint(len(ComputedGloss)) {
				return ComputedGloss[cycle]
			}
		}
	case 0xFD: // STD extended
		switch cycle {
		case 3:
			return " (A)"
		case 4:
			return " (B)"
		}
	case 0xDD: // STD direct
		switch cycle {
		case 2:
			return " (A)"
		case 3:
			return " (B)"
		}
	case 0xBF: // STX extended
		switch cycle {
		case 3:
			return " (Xh)"
		case 4:
			return " (Xl)"
		}
	case 0x9F: // STX direct
		switch cycle {
		case 2:
			return " (Xh)"
		case 3:
			return " (Xl)"
		}
	case 0x10BF: // STY extended
		switch cycle {
		case 1 + 3:
			return " (Yh)"
		case 1 + 4:
			return " (Yl)"
		}
	case 0x109F: // STY direct
		switch cycle {
		case 1 + 2:
			return " (Yh)"
		case 1 + 3:
			return " (Yl)"
		}
	case 0xFF: // STU extended
		switch cycle {
		case 3:
			return " (Uh)"
		case 4:
			return " (Ul)"
		}
	case 0xDF: // STU direct
		switch cycle {
		case 2:
			return " (Uh)"
		case 3:
			return " (Ul)"
		}

	case 0x86: // LDA Immediate
		switch cycle {
		case 1:
			return " (A)"
		}
	case 0x96, 0x97: // LDA, STA Direct
		switch cycle {
		case 2:
			return " (A)"
		}
	case 0xB6, 0xB7: // LDA, STA Extended
		switch cycle {
		case 3:
			return " (A)"
		}

	case 0xC6: // LDB Immediate
		switch cycle {
		case 1:
			return " (B)"
		}
	case 0xD6, 0xD7: // LDB, STB Direct
		switch cycle {
		case 2:
			return " (B)"
		}
	case 0xF6, 0xF7: // LDB, STB Extended
		switch cycle {
		case 3:
			return " (B)"
		}

	case 0xCC: // LDD Immediate
		switch cycle {
		case 1:
			return " (A)"
		case 2:
			return " (B)"
		}
	case 0xDC: // LDD Direct
		switch cycle {
		case 2:
			return " (A)"
		case 3:
			return " (B)"
		}
	case 0xFC: // LDD Extended
		switch cycle {
		case 3:
			return " (A)"
		case 4:
			return " (B)"
		}

	case 0x8E: // LDX Immediate
		switch cycle {
		case 1:
			return " (Xh)"
		case 2:
			return " (Xl)"
		}
	case 0x9E: // LDX Direct
		switch cycle {
		case 2:
			return " (Xh)"
		case 3:
			return " (Xl)"
		}
	case 0xBE: // LDX Extended
		switch cycle {
		case 3:
			return " (Xh)"
		case 4:
			return " (Xl)"
		}

	case 0x108E: // LDY Immediate
		switch cycle {
		case 1 + 1:
			return " (Yh)"
		case 1 + 2:
			return " (Yl)"
		}
	case 0x109E: // LDY Direct
		switch cycle {
		case 1 + 2:
			return " (Yh)"
		case 1 + 3:
			return " (Yl)"
		}
	case 0x10BE: // LDY Extended
		switch cycle {
		case 1 + 3:
			return " (Yh)"
		case 1 + 4:
			return " (Yl)"
		}

	case 0xCE: // LDU Immediate
		switch cycle {
		case 1:
			return " (Uh)"
		case 2:
			return " (Ul)"
		}
	case 0xDE: // LDU Direct
		switch cycle {
		case 2:
			return " (Uh)"
		case 3:
			return " (Ul)"
		}
	case 0xFE: // LDU Extended
		switch cycle {
		case 3:
			return " (Uh)"
		case 4:
			return " (Ul)"
		}

	case 0xA6, 0xA7: // LDA, STA Indexed
		switch cycle {
        case 1:
             IndexedExtra= IndexedCycles(d)
         case IndexedExtra + 2:
			return " (A)"
		}

	case 0xE6, 0xE7: // LDB, STB Indexed
		switch cycle {
        case 1:
             IndexedExtra= IndexedCycles(d)
         case IndexedExtra + 2:
			return " (B)"
		}

	case 0xEC, 0xED: // LDD, STD Indexed
		switch cycle {
        case 1:
             IndexedExtra= IndexedCycles(d)
         case IndexedExtra + 2:
			return " (A)"
        case IndexedExtra + 3:
			return " (B)"
		}

	case 0xAE, 0xAF: // LDX, STX Indexed
		switch cycle {
        case 1:
             IndexedExtra= IndexedCycles(d)
         case IndexedExtra + 2:
			return " (Xh)"
        case IndexedExtra + 3:
			return " (Xl)"
		}

	case 0x10EE, 0x10EF: // LDY, STY Indexed
		switch cycle {
        case 1:
             IndexedExtra= IndexedCycles(d)
         case 1 + IndexedExtra + 2:
			return " (Yh)"
        case 1 + IndexedExtra + 3:
			return " (Yl)"
		}

	case 0xEE, 0xEF: // LDU, STU Indexed
		switch cycle {
        case 1:
             IndexedExtra= IndexedCycles(d)
         case IndexedExtra + 2:
			return " (Uh)"
        case IndexedExtra + 3:
			return " (Ul)"
		}

	}
	return ""
}

func ComputePulsGloss(d byte) []string {
	var z = []string{"", ""}
	for d != 0 {
		switch {
		case (d & 0x01) != 0:
			z = append(z, " (CC)")
			d ^= 0x01
		case (d & 0x02) != 0:
			z = append(z, " (A)")
			d ^= 0x02
		case (d & 0x04) != 0:
			z = append(z, " (B)")
			d ^= 0x04
		case (d & 0x08) != 0:
			z = append(z, " (DP)")
			d ^= 0x08
		case (d & 0x10) != 0:
			z = append(z, " (Xh)", " (Xl)")
			d ^= 0x10
		case (d & 0x20) != 0:
			z = append(z, " (Yh)", " (Yl)")
			d ^= 0x20
		case (d & 0x40) != 0:
			z = append(z, " (Uh)", " (Ul)")
			d ^= 0x40
		case (d & 0x80) != 0:
			z = append(z, " (PCh)", " (PCl)")
			d ^= 0x80
		}
	}
	return z
}

func ComputePshsGloss(d byte) []string {
	var z = []string{"", "", ""}
	for d != 0 {
		switch {
		case (d & 0x80) != 0:
			z = append(z, " (PCl)", " (PCh)")
			d ^= 0x80
		case (d & 0x40) != 0:
			z = append(z, " (Ul)", " (Uh)")
			d ^= 0x40
		case (d & 0x20) != 0:
			z = append(z, " (Yl)", " (Yh)")
			d ^= 0x20
		case (d & 0x10) != 0:
			z = append(z, " (Xl)", " (Xh)")
			d ^= 0x10
		case (d & 0x08) != 0:
			z = append(z, " (DP)")
			d ^= 0x08
		case (d & 0x04) != 0:
			z = append(z, " (B)")
			d ^= 0x04
		case (d & 0x02) != 0:
			z = append(z, " (A)")
			d ^= 0x02
		case (d & 0x01) != 0:
			z = append(z, " (CC)")
			d ^= 0x01
		}
	}
	return z
}

// NOTA BENE: doesn't work in native 6309 mode
var GlossRegsSwi2 = []string{
	"", "", "",
	" (PCl)", " (PCh)",
	" (Ul)", " (Uh)",
	" (Yl)", " (Yh)",
	" (Xl)", " (Xh)",
	" (DP)", " (B)",
	" (A)", " (CC)",
	"", "", "", "",
}

// NOTA BENE: doesn't work in native 6309 mode
var GlossRegsRti = []string{
	"", "",
	" (CC)", " (A)",
	" (B)", " (DP)",
	" (Xh)", " (Xl)",
	" (Yh)", " (Yl)",
	" (Uh)", " (Ul)",
	" (PCh)", " (PCl)",
	"", "", "", "",
}

type IndexedCyclesRecord struct {
	value   byte
	mask    byte
	cycles  uint
	pattern string
}

var IndexedCyclesTable = []IndexedCyclesRecord{
	{0b10000100, 0b10011111, 1, ",R"},
	{0b00000000, 0b10000000, 1, "5,R"},
	{0b10001000, 0b10011111, 1, "8,R"},
	{0b10001001, 0b10011111, 2, "16,R"},

	{0b10000000, 0b10011111, 1, ",R+"},
	{0b10000001, 0b10011111, 1, ",R++"},
	{0b10000010, 0b10011111, 1, ",-R"},
	{0b10000011, 0b10011111, 1, ",--R"},

	{0b10000110, 0b10011111, 1, "A,R"},
	{0b10000101, 0b10011111, 1, "B,R"},
	{0b10000101, 0b10011111, 1, "D,R"},
}

const CyclesUnknown = 999

func IndexedCycles(x byte) uint {
	for _, rec := range IndexedCyclesTable {
		if (x & rec.mask) == rec.value {
			Logf("## pattern %s  -> %d", rec.pattern, rec.cycles)
			return rec.cycles
		}
	}
	return CyclesUnknown
}
