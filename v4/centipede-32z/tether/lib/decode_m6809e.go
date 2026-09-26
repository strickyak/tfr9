package lib

import (
	"fmt"
	"strings"
)

type AddrMode int

const (
	Inherent AddrMode = iota
	Immediate8
	Immediate16
	Direct
	Extended
	Indexed
	Relative8
	Relative16
	PushPull
	ExchangeTransfer
)

type OpInfo struct {
	Name       string
	Mode       AddrMode
	BaseBytes  int
	BaseCycles int
	BaseCodes  string
	Valid      bool
}

var opcodesPage1 [256]OpInfo
var opcodesPage2 [256]OpInfo
var opcodesPage3 [256]OpInfo

func init() {
	opcodesPage1[0x00] = OpInfo{"NEG", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x03] = OpInfo{"COM", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x04] = OpInfo{"LSR", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x06] = OpInfo{"ROR", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x07] = OpInfo{"ASR", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x08] = OpInfo{"ASL", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x09] = OpInfo{"ROL", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x0A] = OpInfo{"DEC", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x0C] = OpInfo{"INC", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x0D] = OpInfo{"TST", Direct, 2, 6, "op-r-", true}
	opcodesPage1[0x0E] = OpInfo{"JMP", Direct, 2, 3, "op-", true}
	opcodesPage1[0x0F] = OpInfo{"CLR", Direct, 2, 6, "op-r-w", true}
	opcodesPage1[0x12] = OpInfo{"NOP", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x13] = OpInfo{"SYNC", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x16] = OpInfo{"LBRA", Relative16, 3, 5, "opp--", true}
	opcodesPage1[0x17] = OpInfo{"LBSR", Relative16, 3, 9, "opp--ww--", true}
	opcodesPage1[0x19] = OpInfo{"DAA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x1A] = OpInfo{"ORCC", Immediate8, 2, 3, "op-", true}
	opcodesPage1[0x1C] = OpInfo{"ANDCC", Immediate8, 2, 3, "op-", true}
	opcodesPage1[0x1D] = OpInfo{"SEX", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x1E] = OpInfo{"EXG", ExchangeTransfer, 2, 8, "op------", true}
	opcodesPage1[0x1F] = OpInfo{"TFR", ExchangeTransfer, 2, 6, "op----", true}
	opcodesPage1[0x20] = OpInfo{"BRA", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x21] = OpInfo{"BRN", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x22] = OpInfo{"BHI", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x23] = OpInfo{"BLS", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x24] = OpInfo{"BCC", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x25] = OpInfo{"BCS", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x26] = OpInfo{"BNE", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x27] = OpInfo{"BEQ", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x28] = OpInfo{"BVC", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x29] = OpInfo{"BVS", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x2A] = OpInfo{"BPL", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x2B] = OpInfo{"BMI", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x2C] = OpInfo{"BGE", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x2D] = OpInfo{"BLT", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x2E] = OpInfo{"BGT", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x2F] = OpInfo{"BLE", Relative8, 2, 3, "op-", true}
	opcodesPage1[0x30] = OpInfo{"LEAX", Indexed, 2, 4, "op--", true}
	opcodesPage1[0x31] = OpInfo{"LEAY", Indexed, 2, 4, "op--", true}
	opcodesPage1[0x32] = OpInfo{"LEAS", Indexed, 2, 4, "op--", true}
	opcodesPage1[0x33] = OpInfo{"LEAU", Indexed, 2, 4, "op--", true}
	opcodesPage1[0x34] = OpInfo{"PSHS", PushPull, 2, 5, "op---", true}
	opcodesPage1[0x35] = OpInfo{"PULS", PushPull, 2, 5, "op---", true}
	opcodesPage1[0x36] = OpInfo{"PSHU", PushPull, 2, 5, "op---", true}
	opcodesPage1[0x37] = OpInfo{"PULU", PushPull, 2, 5, "op---", true}
	opcodesPage1[0x39] = OpInfo{"RTS", Inherent, 1, 5, "o-rr-", true}
	opcodesPage1[0x3B] = OpInfo{"RTI", Inherent, 1, 15, "o-rrrrrrrrrrrr-", true}
	opcodesPage1[0x3C] = OpInfo{"CWAI", Immediate8, 2, 20, "op-wwwwwwwwwwww-----", true}
	opcodesPage1[0x3D] = OpInfo{"MUL", Inherent, 1, 11, "o----------", true}
	opcodesPage1[0x3F] = OpInfo{"SWI", Inherent, 1, 19, "o-wwwwwwwwwwww-----", true}
	opcodesPage1[0x40] = OpInfo{"NEGA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x43] = OpInfo{"COMA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x44] = OpInfo{"LSRA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x46] = OpInfo{"RORA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x47] = OpInfo{"ASRA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x48] = OpInfo{"ASLA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x49] = OpInfo{"ROLA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x4A] = OpInfo{"DECA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x4C] = OpInfo{"INCA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x4D] = OpInfo{"TSTA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x4F] = OpInfo{"CLRA", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x50] = OpInfo{"NEGB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x53] = OpInfo{"COMB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x54] = OpInfo{"LSRB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x56] = OpInfo{"RORB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x57] = OpInfo{"ASRB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x58] = OpInfo{"ASLB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x59] = OpInfo{"ROLB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x5A] = OpInfo{"DECB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x5C] = OpInfo{"INCB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x5D] = OpInfo{"TSTB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x5F] = OpInfo{"CLRB", Inherent, 1, 2, "o-", true}
	opcodesPage1[0x60] = OpInfo{"NEG", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x63] = OpInfo{"COM", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x64] = OpInfo{"LSR", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x66] = OpInfo{"ROR", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x67] = OpInfo{"ASR", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x68] = OpInfo{"ASL", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x69] = OpInfo{"ROL", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x6A] = OpInfo{"DEC", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x6C] = OpInfo{"INC", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x6D] = OpInfo{"TST", Indexed, 2, 6, "op-r-", true}
	opcodesPage1[0x6E] = OpInfo{"JMP", Indexed, 2, 3, "op-", true}
	opcodesPage1[0x6F] = OpInfo{"CLR", Indexed, 2, 6, "op-r-w", true}
	opcodesPage1[0x70] = OpInfo{"NEG", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x73] = OpInfo{"COM", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x74] = OpInfo{"LSR", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x76] = OpInfo{"ROR", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x77] = OpInfo{"ASR", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x78] = OpInfo{"ASL", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x79] = OpInfo{"ROL", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x7A] = OpInfo{"DEC", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x7C] = OpInfo{"INC", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x7D] = OpInfo{"TST", Extended, 3, 6, "opp-r-", true}
	opcodesPage1[0x7E] = OpInfo{"JMP", Extended, 3, 4, "opp-", true}
	opcodesPage1[0x7F] = OpInfo{"CLR", Extended, 3, 7, "opp-r-w", true}
	opcodesPage1[0x80] = OpInfo{"SUBA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x81] = OpInfo{"CMPA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x82] = OpInfo{"SBCA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x83] = OpInfo{"SUBD", Immediate16, 3, 3, "opp", true}
	opcodesPage1[0x84] = OpInfo{"ANDA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x85] = OpInfo{"BITA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x86] = OpInfo{"LDA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x88] = OpInfo{"EORA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x89] = OpInfo{"ADCA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x8A] = OpInfo{"ORA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x8B] = OpInfo{"ADDA", Immediate8, 2, 2, "op", true}
	opcodesPage1[0x8C] = OpInfo{"CMPX", Immediate16, 3, 3, "opp", true}
	opcodesPage1[0x8D] = OpInfo{"BSR", Relative8, 2, 7, "op--ww-", true}
	opcodesPage1[0x8E] = OpInfo{"LDX", Immediate16, 3, 3, "opp", true}
	opcodesPage1[0x90] = OpInfo{"SUBA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x91] = OpInfo{"CMPA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x92] = OpInfo{"SBCA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x93] = OpInfo{"SUBD", Direct, 2, 5, "op-rr", true}
	opcodesPage1[0x94] = OpInfo{"ANDA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x95] = OpInfo{"BITA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x96] = OpInfo{"LDA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x97] = OpInfo{"STA", Direct, 2, 4, "op-w", true}
	opcodesPage1[0x98] = OpInfo{"EORA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x99] = OpInfo{"ADCA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x9A] = OpInfo{"ORA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x9B] = OpInfo{"ADDA", Direct, 2, 4, "op-r", true}
	opcodesPage1[0x9C] = OpInfo{"CMPX", Direct, 2, 5, "op-rr", true}
	opcodesPage1[0x9D] = OpInfo{"JSR", Direct, 2, 7, "op--ww-", true}
	opcodesPage1[0x9E] = OpInfo{"LDX", Direct, 2, 5, "op-rr", true}
	opcodesPage1[0x9F] = OpInfo{"STX", Direct, 2, 5, "op-ww", true}
	opcodesPage1[0xA0] = OpInfo{"SUBA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA1] = OpInfo{"CMPA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA2] = OpInfo{"SBCA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA3] = OpInfo{"SUBD", Indexed, 2, 5, "op-rr", true}
	opcodesPage1[0xA4] = OpInfo{"ANDA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA5] = OpInfo{"BITA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA6] = OpInfo{"LDA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA7] = OpInfo{"STA", Indexed, 2, 4, "op-w", true}
	opcodesPage1[0xA8] = OpInfo{"EORA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xA9] = OpInfo{"ADCA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xAA] = OpInfo{"ORA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xAB] = OpInfo{"ADDA", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xAC] = OpInfo{"CMPX", Indexed, 2, 5, "op-rr", true}
	opcodesPage1[0xAD] = OpInfo{"JSR", Indexed, 2, 7, "op--ww-", true}
	opcodesPage1[0xAE] = OpInfo{"LDX", Indexed, 2, 5, "op-rr", true}
	opcodesPage1[0xAF] = OpInfo{"STX", Indexed, 2, 5, "op-ww", true}
	opcodesPage1[0xB0] = OpInfo{"SUBA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB1] = OpInfo{"CMPA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB2] = OpInfo{"SBCA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB3] = OpInfo{"SUBD", Extended, 3, 6, "opp-rr", true}
	opcodesPage1[0xB4] = OpInfo{"ANDA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB5] = OpInfo{"BITA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB6] = OpInfo{"LDA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB7] = OpInfo{"STA", Extended, 3, 5, "opp-w", true}
	opcodesPage1[0xB8] = OpInfo{"EORA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xB9] = OpInfo{"ADCA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xBA] = OpInfo{"ORA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xBB] = OpInfo{"ADDA", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xBC] = OpInfo{"CMPX", Extended, 3, 6, "opp-rr", true}
	opcodesPage1[0xBD] = OpInfo{"JSR", Extended, 3, 8, "opp--ww-", true}
	opcodesPage1[0xBE] = OpInfo{"LDX", Extended, 3, 6, "opp-rr", true}
	opcodesPage1[0xBF] = OpInfo{"STX", Extended, 3, 6, "opp-ww", true}
	opcodesPage1[0xC0] = OpInfo{"SUBB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC1] = OpInfo{"CMPB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC2] = OpInfo{"SBCB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC3] = OpInfo{"ADDD", Immediate16, 3, 3, "opp", true}
	opcodesPage1[0xC4] = OpInfo{"ANDB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC5] = OpInfo{"BITB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC6] = OpInfo{"LDB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC8] = OpInfo{"EORB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xC9] = OpInfo{"ADCB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xCA] = OpInfo{"ORB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xCB] = OpInfo{"ADDB", Immediate8, 2, 2, "op", true}
	opcodesPage1[0xCC] = OpInfo{"LDD", Immediate16, 3, 3, "opp", true}
	opcodesPage1[0xCE] = OpInfo{"LDU", Immediate16, 3, 3, "opp", true}
	opcodesPage1[0xD0] = OpInfo{"SUBB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD1] = OpInfo{"CMPB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD2] = OpInfo{"SBCB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD3] = OpInfo{"ADDD", Direct, 2, 5, "op-rr", true}
	opcodesPage1[0xD4] = OpInfo{"ANDB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD5] = OpInfo{"BITB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD6] = OpInfo{"LDB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD7] = OpInfo{"STB", Direct, 2, 4, "op-w", true}
	opcodesPage1[0xD8] = OpInfo{"EORB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xD9] = OpInfo{"ADCB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xDA] = OpInfo{"ORB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xDB] = OpInfo{"ADDB", Direct, 2, 4, "op-r", true}
	opcodesPage1[0xDC] = OpInfo{"LDD", Direct, 2, 5, "op-rr", true}
	opcodesPage1[0xDD] = OpInfo{"STD", Direct, 2, 5, "op-ww", true}
	opcodesPage1[0xDE] = OpInfo{"LDU", Direct, 2, 5, "op-rr", true}
	opcodesPage1[0xDF] = OpInfo{"STU", Direct, 2, 5, "op-ww", true}
	opcodesPage1[0xE0] = OpInfo{"SUBB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE1] = OpInfo{"CMPB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE2] = OpInfo{"SBCB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE3] = OpInfo{"ADDD", Indexed, 2, 5, "op-rr", true}
	opcodesPage1[0xE4] = OpInfo{"ANDB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE5] = OpInfo{"BITB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE6] = OpInfo{"LDB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE7] = OpInfo{"STB", Indexed, 2, 4, "op-w", true}
	opcodesPage1[0xE8] = OpInfo{"EORB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xE9] = OpInfo{"ADCB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xEA] = OpInfo{"ORB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xEB] = OpInfo{"ADDB", Indexed, 2, 4, "op-r", true}
	opcodesPage1[0xEC] = OpInfo{"LDD", Indexed, 2, 5, "op-rr", true}
	opcodesPage1[0xED] = OpInfo{"STD", Indexed, 2, 5, "op-ww", true}
	opcodesPage1[0xEE] = OpInfo{"LDU", Indexed, 2, 5, "op-rr", true}
	opcodesPage1[0xEF] = OpInfo{"STU", Indexed, 2, 5, "op-ww", true}
	opcodesPage1[0xF0] = OpInfo{"SUBB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF1] = OpInfo{"CMPB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF2] = OpInfo{"SBCB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF3] = OpInfo{"ADDD", Extended, 3, 6, "opp-rr", true}
	opcodesPage1[0xF4] = OpInfo{"ANDB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF5] = OpInfo{"BITB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF6] = OpInfo{"LDB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF7] = OpInfo{"STB", Extended, 3, 5, "opp-w", true}
	opcodesPage1[0xF8] = OpInfo{"EORB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xF9] = OpInfo{"ADCB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xFA] = OpInfo{"ORB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xFB] = OpInfo{"ADDB", Extended, 3, 5, "opp-r", true}
	opcodesPage1[0xFC] = OpInfo{"LDD", Extended, 3, 6, "opp-rr", true}
	opcodesPage1[0xFD] = OpInfo{"STD", Extended, 3, 6, "opp-ww", true}
	opcodesPage1[0xFE] = OpInfo{"LDU", Extended, 3, 6, "opp-rr", true}
	opcodesPage1[0xFF] = OpInfo{"STU", Extended, 3, 6, "opp-ww", true}
	opcodesPage2[0x20] = OpInfo{"LBRA", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x21] = OpInfo{"LBRN", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x22] = OpInfo{"LBHI", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x23] = OpInfo{"LBLS", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x24] = OpInfo{"LBCC", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x25] = OpInfo{"LBCS", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x26] = OpInfo{"LBNE", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x27] = OpInfo{"LBEQ", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x28] = OpInfo{"LBVC", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x29] = OpInfo{"LBVS", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x2A] = OpInfo{"LBPL", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x2B] = OpInfo{"LBMI", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x2C] = OpInfo{"LBGE", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x2D] = OpInfo{"LBLT", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x2E] = OpInfo{"LBGT", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x2F] = OpInfo{"LBLE", Relative16, 4, 6, "oopp--", true}
	opcodesPage2[0x3F] = OpInfo{"SWI2", Inherent, 2, 20, "oo-wwwwwwwwwwww-----", true}
	opcodesPage2[0x83] = OpInfo{"CMPD", Immediate16, 4, 4, "oopp", true}
	opcodesPage2[0x8C] = OpInfo{"CMPY", Immediate16, 4, 4, "oopp", true}
	opcodesPage2[0x8E] = OpInfo{"LDY", Immediate16, 4, 4, "oopp", true}
	opcodesPage2[0x93] = OpInfo{"CMPD", Direct, 3, 6, "oop-rr", true}
	opcodesPage2[0x9C] = OpInfo{"CMPY", Direct, 3, 6, "oop-rr", true}
	opcodesPage2[0x9E] = OpInfo{"LDY", Direct, 3, 6, "oop-rr", true}
	opcodesPage2[0x9F] = OpInfo{"STY", Direct, 3, 6, "oop-ww", true}
	opcodesPage2[0xA3] = OpInfo{"CMPD", Indexed, 3, 6, "oop-rr", true}
	opcodesPage2[0xAC] = OpInfo{"CMPY", Indexed, 3, 6, "oop-rr", true}
	opcodesPage2[0xAE] = OpInfo{"LDY", Indexed, 3, 6, "oop-rr", true}
	opcodesPage2[0xAF] = OpInfo{"STY", Indexed, 3, 6, "oop-ww", true}
	opcodesPage2[0xB3] = OpInfo{"CMPD", Extended, 4, 7, "oopp-rr", true}
	opcodesPage2[0xBC] = OpInfo{"CMPY", Extended, 4, 7, "oopp-rr", true}
	opcodesPage2[0xBE] = OpInfo{"LDY", Extended, 4, 7, "oopp-rr", true}
	opcodesPage2[0xBF] = OpInfo{"STY", Extended, 4, 7, "oopp-ww", true}
	opcodesPage2[0xCE] = OpInfo{"LDS", Immediate16, 4, 4, "oopp", true}
	opcodesPage2[0xDE] = OpInfo{"LDS", Direct, 3, 6, "oop-rr", true}
	opcodesPage2[0xDF] = OpInfo{"STS", Direct, 3, 6, "oop-ww", true}
	opcodesPage2[0xEE] = OpInfo{"LDS", Indexed, 3, 6, "oop-rr", true}
	opcodesPage2[0xEF] = OpInfo{"STS", Indexed, 3, 6, "oop-ww", true}
	opcodesPage2[0xFE] = OpInfo{"LDS", Extended, 4, 7, "oopp-rr", true}
	opcodesPage2[0xFF] = OpInfo{"STS", Extended, 4, 7, "oopp-ww", true}
	opcodesPage3[0x3F] = OpInfo{"SWI3", Inherent, 2, 20, "oo-wwwwwwwwwwww-----", true}
	opcodesPage3[0x83] = OpInfo{"CMPU", Immediate16, 4, 4, "oopp", true}
	opcodesPage3[0x8C] = OpInfo{"CMPS", Immediate16, 4, 4, "oopp", true}
	opcodesPage3[0x93] = OpInfo{"CMPU", Direct, 3, 6, "oop-rr", true}
	opcodesPage3[0x9C] = OpInfo{"CMPS", Direct, 3, 6, "oop-rr", true}
	opcodesPage3[0xA3] = OpInfo{"CMPU", Indexed, 3, 6, "oop-rr", true}
	opcodesPage3[0xAC] = OpInfo{"CMPS", Indexed, 3, 6, "oop-rr", true}
	opcodesPage3[0xB3] = OpInfo{"CMPU", Extended, 4, 7, "oopp-rr", true}
	opcodesPage3[0xBC] = OpInfo{"CMPS", Extended, 4, 7, "oopp-rr", true}

}

func Decode(mem []byte) (disasm string, numBytes int, numCycles int, cycleCodes string, ok bool) {
	if len(mem) == 0 {
		return "", 0, 0, "", false
	}

	opByte := mem[0]
	page := 1
	offset := 1

	var table *[256]OpInfo
	if opByte == 0x10 {
		page = 2
		if len(mem) < 2 {
			return "", 0, 0, "", false
		}
		opByte = mem[1]
		offset = 2
		table = &opcodesPage2
	} else if opByte == 0x11 {
		page = 3
		if len(mem) < 2 {
			return "", 0, 0, "", false
		}
		opByte = mem[1]
		offset = 2
		table = &opcodesPage3
	} else {
		table = &opcodesPage1
	}

	info := table[opByte]
	if !info.Valid {
		return "", 0, 0, "", false
	}

	numBytes = info.BaseBytes
	numCycles = info.BaseCycles
	cycleCodes = info.BaseCodes

	if len(mem) < numBytes && info.Mode != Indexed && info.Mode != PushPull {
		return "", 0, 0, "", false
	}

	switch info.Mode {
	case Inherent:
		disasm = info.Name
	case Immediate8:
		val := mem[offset]
		disasm = fmt.Sprintf("%s #$%02X", info.Name, val)
	case Immediate16:
		val := (uint16(mem[offset]) << 8) | uint16(mem[offset+1])
		disasm = fmt.Sprintf("%s #$%04X", info.Name, val)
	case Direct:
		val := mem[offset]
		disasm = fmt.Sprintf("%s <$%02X", info.Name, val)
	case Extended:
		val := (uint16(mem[offset]) << 8) | uint16(mem[offset+1])
		disasm = fmt.Sprintf("%s >$%04X", info.Name, val)
	case Relative8:
		val := int8(mem[offset])
		disasm = fmt.Sprintf("%s %d", info.Name, val)
	case Relative16:
		val := int16((uint16(mem[offset]) << 8) | uint16(mem[offset+1]))
		disasm = fmt.Sprintf("%s %d", info.Name, val)
	case ExchangeTransfer:
		pb := mem[offset]
		r1 := getRegName((pb >> 4) & 0xF)
		r2 := getRegName(pb & 0xF)
		disasm = fmt.Sprintf("%s %s,%s", info.Name, r1, r2)
	case PushPull:
		if len(mem) < numBytes {
			return "", 0, 0, "", false
		}
		pb := mem[offset]
		regs := []string{}
		bits := []string{"CC", "A", "B", "DP", "X", "Y", "U_S", "PC"}
		if info.Name == "PSHU" || info.Name == "PULU" {
			bits[6] = "S"
		} else {
			bits[6] = "U"
		}

		count := 0
		for i := 0; i < 8; i++ {
			if (pb & (1 << i)) != 0 {
				regs = append(regs, bits[i])
				if bits[i] == "X" || bits[i] == "Y" || bits[i] == "U_S" || bits[i] == "U" || bits[i] == "S" || bits[i] == "PC" {
					count += 2
				} else {
					count++
				}
			}
		}

		disasm = fmt.Sprintf("%s %s", info.Name, strings.Join(regs, ","))
		numCycles += count

		char := "w"
		if info.Name == "PULS" || info.Name == "PULU" {
			char = "r"
		}
		if len(cycleCodes) > 0 {
			// cycle string op--- should become op---www
			// wait, we just append count times of r/w, and subtract one -
			// let's do: baseCodes is op---
			// We'll replace the last '-' with string
			if len(cycleCodes) > 0 {
				cycleCodes = cycleCodes[:len(cycleCodes)-1] + strings.Repeat(char, count) + "-"
			}
		}
	case Indexed:
		if len(mem) < offset+1 {
			return "", 0, 0, "", false
		}
		pb := mem[offset]
		idxDisasm, extraBytes, extraCycles, extraCodes := decodeIndexed(pb, mem, offset)
		if extraBytes < 0 {
			return "", 0, 0, "", false
		}
		disasm = fmt.Sprintf("%s %s", info.Name, idxDisasm)
		numBytes += extraBytes
		numCycles += extraCycles
		cycleCodes = insertIndexedCodes(cycleCodes, extraCodes, page)
	}

	if len(mem) < numBytes {
		return "", 0, 0, "", false
	}

	return disasm, numBytes, numCycles, cycleCodes, true
}

func getRegName(r uint8) string {
	switch r {
	case 0:
		return "D"
	case 1:
		return "X"
	case 2:
		return "Y"
	case 3:
		return "U"
	case 4:
		return "S"
	case 5:
		return "PC"
	case 8:
		return "A"
	case 9:
		return "B"
	case 10:
		return "CC"
	case 11:
		return "DP"
	default:
		return "INV"
	}
}

func decodeIndexed(pb byte, mem []byte, offset int) (string, int, int, string) {
	regBits := (pb >> 5) & 3
	var reg string
	switch regBits {
	case 0:
		reg = "X"
	case 1:
		reg = "Y"
	case 2:
		reg = "U"
	case 3:
		reg = "S"
	}

	if (pb & 0x80) == 0 {
		val := int8(pb & 0x1F)
		if val > 15 {
			val -= 32
		}
		return fmt.Sprintf("%d,%s", val, reg), 0, 1, "-"
	}

	indirect := (pb & 0x10) != 0
	mod := pb & 0x0F

	extraBytes := 0
	extraCycles := 0
	extraCodes := ""
	disasm := ""

	switch mod {
	case 0x00: // ,R+
		disasm = fmt.Sprintf(",%s+", reg)
		extraCycles = 2
		extraCodes = "--"
	case 0x01: // ,R++
		disasm = fmt.Sprintf(",%s++", reg)
		extraCycles = 3
		extraCodes = "---"
	case 0x02: // ,-R
		disasm = fmt.Sprintf(",-%s", reg)
		extraCycles = 2
		extraCodes = "--"
	case 0x03: // ,--R
		disasm = fmt.Sprintf(",--%s", reg)
		extraCycles = 3
		extraCodes = "---"
	case 0x04: // ,R
		disasm = fmt.Sprintf(",%s", reg)
		extraCycles = 0
		extraCodes = ""
	case 0x05: // B,R
		disasm = fmt.Sprintf("B,%s", reg)
		extraCycles = 1
		extraCodes = "-"
	case 0x06: // A,R
		disasm = fmt.Sprintf("A,%s", reg)
		extraCycles = 1
		extraCodes = "-"
	case 0x08: // 8-bit offset
		extraBytes = 1
		if len(mem) < offset+2 {
			return "", -1, 0, ""
		}
		val := int8(mem[offset+1])
		disasm = fmt.Sprintf("%d,%s", val, reg)
		extraCycles = 1
		extraCodes = "p-"
	case 0x09: // 16-bit offset
		extraBytes = 2
		if len(mem) < offset+3 {
			return "", -1, 0, ""
		}
		val := int16((uint16(mem[offset+1]) << 8) | uint16(mem[offset+2]))
		disasm = fmt.Sprintf("%d,%s", val, reg)
		extraCycles = 4
		extraCodes = "pp--"
	case 0x0B: // D,R
		disasm = fmt.Sprintf("D,%s", reg)
		extraCycles = 4
		extraCodes = "----"
	case 0x0C: // 8-bit PCR
		extraBytes = 1
		if len(mem) < offset+2 {
			return "", -1, 0, ""
		}
		val := int8(mem[offset+1])
		disasm = fmt.Sprintf("%d,PCR", val)
		extraCycles = 1
		extraCodes = "p-"
	case 0x0D: // 16-bit PCR
		extraBytes = 2
		if len(mem) < offset+3 {
			return "", -1, 0, ""
		}
		val := int16((uint16(mem[offset+1]) << 8) | uint16(mem[offset+2]))
		disasm = fmt.Sprintf("%d,PCR", val)
		extraCycles = 5
		extraCodes = "pp---"
	case 0x0F: // Extended indirect
		extraBytes = 2
		if len(mem) < offset+3 {
			return "", -1, 0, ""
		}
		val := uint16((uint16(mem[offset+1]) << 8) | uint16(mem[offset+2]))
		disasm = fmt.Sprintf(">$%04X", val)
		extraCycles = 1
		extraCodes = "pp-"
	default:
		return "", -1, 0, ""
	}

	if indirect {
		if mod == 0x00 || mod == 0x02 {
			return "", -1, 0, ""
		}
		disasm = "[" + disasm + "]"
		extraCycles += 3
		extraCodes += "rr-"
	}

	return disasm, extraBytes, extraCycles, extraCodes
}

func insertIndexedCodes(base string, extra string, page int) string {
	idx := 2
	if page > 1 {
		idx = 3
	}
	if len(base) >= idx {
		return base[:idx] + extra + base[idx:]
	}
	return base + extra
}
