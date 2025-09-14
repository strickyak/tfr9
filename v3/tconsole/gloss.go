package main

var gloss uint
var cycle uint

func GlossFirstCycle(_ uint, d byte) string {
	cycle = 0
	gloss = uint(d)
	return ""
}

func GlossLaterCycle(_ uint, d byte) string {
	cycle++
	switch gloss {
	case 0x10: // prefix $10
		gloss = 0x1000 | uint(d)
	case 0x103F: // SWI2
		return GlossRegsSwi2[cycle]
	case 0x3B: // RTI
		return GlossRegsRti[cycle]
	}
	return ""
}

var GlossRegsSwi2 = []string{
	"", "", "",
	" (PCl)", " (PCh)",
	" (Ul)", " (Uh)",
	" (Yl)", " (Yh)",
	" (Xl)", " (Xh)",
	" (DP)", " (B)",
	" (A)", " (CC)",
	" (?1)", " (?2)", " (?3)", " (?4)",
}

var GlossRegsRti = []string{
	"", "",
	" (CC)", " (A)",
	" (B)", " (DP)",
	" (Xh)", " (Xl)",
	" (Yh)", " (Yl)",
	" (Uh)", " (Ul)",
	" (PCh)", " (PCl)",
	" (?1)", " (?2)", " (?3)", " (?4)",
}
