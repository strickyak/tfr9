#!/usr/bin/env python3
"""
dis6809.py - MC6809 Disassembler
"""

import sys
import argparse

REG_NAMES = ['D', 'X', 'Y', 'U', 'S', 'PC', '?', '?', 'A', 'B', 'CC', 'DP']
POST_REG = ['X', 'Y', 'U', 'S']

def psh_pul_list(post, is_s=True):
    r_alt = 'U' if is_s else 'S'
    names = ['CC', 'A', 'B', 'DP', 'X', 'Y', r_alt, 'PC']
    regs = []
    for i in range(8):
        if post & (1 << i):
            regs.append(names[i])
    return ','.join(regs)

def decode_indexed(data, pc, base_addr):
    if pc >= len(data):
        return "???", pc
    pb = data[pc]
    pc += 1

    reg = POST_REG[(pb >> 5) & 3]
    ind = bool(pb & 0x10)

    if (pb & 0x80) == 0:
        # 5-bit offset (-16..15)
        offset = pb & 0x1F
        if offset & 0x10:
            offset -= 32
        return f"{offset},{reg}", pc

    mode = pb & 0x0F
    if mode == 0: # ,R+
        s = f",{reg}+"
    elif mode == 1: # ,R++
        s = f",{reg}++"
    elif mode == 2: # ,-R
        s = f",-{reg}"
    elif mode == 3: # ,--R
        s = f",--{reg}"
    elif mode == 4: # ,R
        s = f",{reg}"
    elif mode == 5: # B,R
        s = f"B,{reg}"
    elif mode == 6: # A,R
        s = f"A,{reg}"
    elif mode == 7:
        s = f"??? ({pb:02X})"
    elif mode == 8: # 8-bit offset
        if pc >= len(data): return "???", pc
        off = data[pc]; pc += 1
        if off & 0x80: off -= 256
        s = f"{off},{reg}"
    elif mode == 9: # 16-bit offset
        if pc + 1 >= len(data): return "???", pc
        off = (data[pc] << 8) | data[pc+1]; pc += 2
        if off & 0x8000: off -= 65536
        s = f"${off & 0xFFFF:04X},{reg}"
    elif mode == 11: # D,R
        s = f"D,{reg}"
    elif mode == 12: # 8-bit PC relative
        if pc >= len(data): return "???", pc
        off = data[pc]; pc += 1
        if off & 0x80: off -= 256
        target = (base_addr + pc + off) & 0xFFFF
        s = f"${target:04X},PCR"
    elif mode == 13: # 16-bit PC relative
        if pc + 1 >= len(data): return "???", pc
        off = (data[pc] << 8) | data[pc+1]; pc += 2
        if off & 0x8000: off -= 65536
        target = (base_addr + pc + off) & 0xFFFF
        s = f"${target:04X},PCR"
    elif mode == 15: # Extended indirect
        if pc + 1 >= len(data): return "???", pc
        addr = (data[pc] << 8) | data[pc+1]; pc += 2
        return f"[${addr:04X}]", pc
    else:
        s = f"??? ({pb:02X})"

    if ind:
        return f"[{s}]", pc
    return s, pc

# Mode constants
INH = 0
IMM8 = 1
IMM16 = 2
DIR = 3
EXT = 4
IND = 5
REL8 = 6
REL16 = 7
PSHS = 8
PULS = 9
PSHU = 10
PULU = 11
EXG = 12
TFR = 13

# Opcode table: (mnemonic, mode)
OP_PAGE0 = {
    0x00: ('NEG', DIR), 0x03: ('COM', DIR), 0x04: ('LSR', DIR), 0x06: ('ROR', DIR),
    0x07: ('ASR', DIR), 0x08: ('ASL', DIR), 0x09: ('ROL', DIR), 0x0A: ('DEC', DIR),
    0x0C: ('INC', DIR), 0x0D: ('TST', DIR), 0x0E: ('JMP', DIR), 0x0F: ('CLR', DIR),

    0x12: ('NOP', INH), 0x13: ('SYNC', INH), 0x16: ('LBRA', REL16), 0x17: ('LBSR', REL16),
    0x19: ('DAA', INH), 0x1A: ('ORCC', IMM8), 0x1C: ('ANDCC', IMM8), 0x1D: ('SEX', INH),
    0x1E: ('EXG', EXG), 0x1F: ('TFR', TFR),

    0x20: ('BRA', REL8), 0x21: ('BRN', REL8), 0x22: ('BHI', REL8), 0x23: ('BLS', REL8),
    0x24: ('BCC', REL8), 0x25: ('BCS', REL8), 0x26: ('BNE', REL8), 0x27: ('BEQ', REL8),
    0x28: ('BVC', REL8), 0x29: ('BVS', REL8), 0x2A: ('BPL', REL8), 0x2B: ('BMI', REL8),
    0x2C: ('BGE', REL8), 0x2D: ('BLT', REL8), 0x2E: ('BGT', REL8), 0x2F: ('BLE', REL8),

    0x30: ('LEAX', IND), 0x31: ('LEAY', IND), 0x32: ('LEAS', IND), 0x33: ('LEAU', IND),
    0x34: ('PSHS', PSHS), 0x35: ('PULS', PULS), 0x36: ('PSHU', PSHU), 0x37: ('PULU', PULU),
    0x39: ('RTS', INH), 0x3A: ('ABX', INH), 0x3B: ('RTI', INH), 0x3C: ('CWAI', IMM8),
    0x3D: ('MUL', INH), 0x3F: ('SWI', INH),

    0x40: ('NEGA', INH), 0x43: ('COMA', INH), 0x44: ('LSRA', INH), 0x46: ('RORA', INH),
    0x47: ('ASRA', INH), 0x48: ('ASLA', INH), 0x49: ('ROLA', INH), 0x4A: ('DECA', INH),
    0x4C: ('INCA', INH), 0x4D: ('TSTA', INH), 0x4F: ('CLRA', INH),

    0x50: ('NEGB', INH), 0x53: ('COMB', INH), 0x54: ('LSRB', INH), 0x56: ('RORB', INH),
    0x57: ('ASRB', INH), 0x58: ('ASLB', INH), 0x59: ('ROLB', INH), 0x5A: ('DECB', INH),
    0x5C: ('INCB', INH), 0x5D: ('TSTB', INH), 0x5F: ('CLRB', INH),

    0x60: ('NEG', IND), 0x63: ('COM', IND), 0x64: ('LSR', IND), 0x66: ('ROR', IND),
    0x67: ('ASR', IND), 0x68: ('ASL', IND), 0x69: ('ROL', IND), 0x6A: ('DEC', IND),
    0x6C: ('INC', IND), 0x6D: ('TST', IND), 0x6E: ('JMP', IND), 0x6F: ('CLR', IND),

    0x70: ('NEG', EXT), 0x73: ('COM', EXT), 0x74: ('LSR', EXT), 0x76: ('ROR', EXT),
    0x77: ('ASR', EXT), 0x78: ('ASL', EXT), 0x79: ('ROL', EXT), 0x7A: ('DEC', EXT),
    0x7C: ('INC', EXT), 0x7D: ('TST', EXT), 0x7E: ('JMP', EXT), 0x7F: ('CLR', EXT),

    # 0x80 - 0xBF: ALU A & B
    0x80: ('SUBA', IMM8), 0x81: ('CMPA', IMM8), 0x82: ('SBCA', IMM8), 0x83: ('SUBD', IMM16),
    0x84: ('ANDA', IMM8), 0x85: ('BITA', IMM8), 0x86: ('LDA', IMM8),  0x88: ('EORA', IMM8),
    0x89: ('ADCA', IMM8), 0x8A: ('ORA', IMM8),  0x8B: ('ADDA', IMM8), 0x8C: ('CMPX', IMM16),
    0x8D: ('BSR', REL8),  0x8E: ('LDX', IMM16),

    0x90: ('SUBA', DIR),  0x91: ('CMPA', DIR),  0x92: ('SBCA', DIR),  0x93: ('SUBD', DIR),
    0x94: ('ANDA', DIR),  0x95: ('BITA', DIR),  0x96: ('LDA', DIR),   0x97: ('STA', DIR),
    0x98: ('EORA', DIR),  0x99: ('ADCA', DIR),  0x9A: ('ORA', DIR),   0x9B: ('ADDA', DIR),
    0x9C: ('CMPX', DIR),  0x9D: ('JSR', DIR),   0x9E: ('LDX', DIR),   0x9F: ('STX', DIR),

    0xA0: ('SUBA', IND),  0xA1: ('CMPA', IND),  0xA2: ('SBCA', IND),  0xA3: ('SUBD', IND),
    0xA4: ('ANDA', IND),  0xA5: ('BITA', IND),  0xA6: ('LDA', IND),   0xA7: ('STA', IND),
    0xA8: ('EORA', IND),  0xA9: ('ADCA', IND),  0xAA: ('ORA', IND),   0xAB: ('ADDA', IND),
    0xAC: ('CMPX', IND),  0xAD: ('JSR', IND),   0xAE: ('LDX', IND),   0xAF: ('STX', IND),

    0xB0: ('SUBA', EXT),  0xB1: ('CMPA', EXT),  0xB2: ('SBCA', EXT),  0xB3: ('SUBD', EXT),
    0xB4: ('ANDA', EXT),  0xB5: ('BITA', EXT),  0xB6: ('LDA', EXT),   0xB7: ('STA', EXT),
    0xB8: ('EORA', EXT),  0xB9: ('ADCA', EXT),  0xBA: ('ORA', EXT),   0xBB: ('ADDA', EXT),
    0xBC: ('CMPX', EXT),  0xBD: ('JSR', EXT),   0xBE: ('LDX', EXT),   0xBF: ('STX', EXT),

    0xC0: ('SUBB', IMM8), 0xC1: ('CMPB', IMM8), 0xC2: ('SBCB', IMM8), 0xC3: ('ADDD', IMM16),
    0xC4: ('ANDB', IMM8), 0xC5: ('BITB', IMM8), 0xC6: ('LDB', IMM8),  0xC8: ('EORB', IMM8),
    0xC9: ('ADCB', IMM8), 0xCA: ('ORB', IMM8),  0xCB: ('ADDB', IMM8), 0xCC: ('LDD', IMM16),
    0xCE: ('LDU', IMM16),

    0xD0: ('SUBB', DIR),  0xD1: ('CMPB', DIR),  0xD2: ('SBCB', DIR),  0xD3: ('ADDD', DIR),
    0xD4: ('ANDB', DIR),  0xD5: ('BITB', DIR),  0xD6: ('LDB', DIR),   0xD7: ('STB', DIR),
    0xD8: ('EORB', DIR),  0xD9: ('ADCB', DIR),  0xDA: ('ORB', DIR),   0xDB: ('ADDB', DIR),
    0xDC: ('LDD', DIR),   0xDD: ('STD', DIR),   0xDE: ('LDU', DIR),   0xDF: ('STU', DIR),

    0xE0: ('SUBB', IND),  0xE1: ('CMPB', IND),  0xE2: ('SBCB', IND),  0xE3: ('ADDD', IND),
    0xE4: ('ANDB', IND),  0xE5: ('BITB', IND),  0xE6: ('LDB', IND),   0xE7: ('STB', IND),
    0xE8: ('EORB', IND),  0xE9: ('ADCB', IND),  0xEA: ('ORB', IND),   0xEB: ('ADDB', IND),
    0xEC: ('LDD', IND),   0xED: ('STD', IND),   0xEE: ('LDU', IND),   0xEF: ('STU', IND),

    0xF0: ('SUBB', EXT),  0xF1: ('CMPB', EXT),  0xF2: ('SBCB', EXT),  0xF3: ('ADDD', EXT),
    0xF4: ('ANDB', EXT),  0xF5: ('BITB', EXT),  0xF6: ('LDB', EXT),   0xF7: ('STB', EXT),
    0xF8: ('EORB', EXT),  0xF9: ('ADCB', EXT),  0xFA: ('ORB', EXT),   0xFB: ('ADDB', EXT),
    0xFC: ('LDD', EXT),   0xFD: ('STD', EXT),   0xFE: ('LDU', EXT),   0xFF: ('STU', EXT),
}

OP_PAGE1 = { # Prefix 0x10
    0x21: ('LBRN', REL16), 0x22: ('LBHI', REL16), 0x23: ('LBLS', REL16),
    0x24: ('LBCC', REL16), 0x25: ('LBCS', REL16), 0x26: ('LBNE', REL16), 0x27: ('LBEQ', REL16),
    0x28: ('LBVC', REL16), 0x29: ('LBVS', REL16), 0x2A: ('LBPL', REL16), 0x2B: ('LBMI', REL16),
    0x2C: ('LBGE', REL16), 0x2D: ('LBLT', REL16), 0x2E: ('LBGT', REL16), 0x2F: ('LBLE', REL16),
    0x3F: ('SWI2', INH),
    0x83: ('CMPD', IMM16), 0x8C: ('CMPY', IMM16), 0x8E: ('LDY', IMM16),
    0x93: ('CMPD', DIR),   0x9C: ('CMPY', DIR),   0x9E: ('LDY', DIR),   0x9F: ('STY', DIR),
    0xA3: ('CMPD', IND),   0xAC: ('CMPY', IND),   0xAE: ('LDY', IND),   0xAF: ('STY', IND),
    0xB3: ('CMPD', EXT),   0xBC: ('CMPY', EXT),   0xBE: ('LDY', EXT),   0xBF: ('STY', EXT),
    0xCE: ('LDS', IMM16),
    0xDE: ('LDS', DIR),    0xDF: ('STS', DIR),
    0xEE: ('LDS', IND),    0xEF: ('STS', IND),
    0xFE: ('LDS', EXT),    0xFF: ('STS', EXT),
}

OP_PAGE2 = { # Prefix 0x11
    0x3F: ('SWI3', INH),
    0x83: ('CMPU', IMM16), 0x8C: ('CMPS', IMM16),
    0x93: ('CMPU', DIR),   0x9C: ('CMPS', DIR),
    0xA3: ('CMPU', IND),   0xAC: ('CMPS', IND),
    0xB3: ('CMPU', EXT),   0xBC: ('CMPS', EXT),
}

def disassemble_one(data, pc, base_addr):
    start_pc = pc
    op = data[pc]
    pc += 1

    table = OP_PAGE0
    if op == 0x10:
        if pc >= len(data): return f"{base_addr+start_pc:04X}: 10        ???", pc
        op = data[pc]; pc += 1
        table = OP_PAGE1
    elif op == 0x11:
        if pc >= len(data): return f"{base_addr+start_pc:04X}: 11        ???", pc
        op = data[pc]; pc += 1
        table = OP_PAGE2

    if op not in table:
        raw = ' '.join(f"{b:02X}" for b in data[start_pc:pc])
        return f"{base_addr+start_pc:04X}: {raw:<12} ??? ({op:02X})", pc

    mnem, mode = table[op]
    operand = ""

    if mode == INH:
        operand = ""
    elif mode == IMM8:
        if pc >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        val = data[pc]; pc += 1
        operand = f"#${val:02X}"
    elif mode == IMM16:
        if pc + 1 >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        val = (data[pc] << 8) | data[pc+1]; pc += 2
        operand = f"#${val:04X}"
    elif mode == DIR:
        if pc >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        val = data[pc]; pc += 1
        operand = f"<${val:02X}"
    elif mode == EXT:
        if pc + 1 >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        val = (data[pc] << 8) | data[pc+1]; pc += 2
        operand = f">${val:04X}"
    elif mode == IND:
        operand, pc = decode_indexed(data, pc, base_addr)
    elif mode == REL8:
        if pc >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        rel = data[pc]; pc += 1
        if rel & 0x80: rel -= 256
        target = (base_addr + pc + rel) & 0xFFFF
        operand = f"${target:04X}"
    elif mode == REL16:
        if pc + 1 >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        rel = (data[pc] << 8) | data[pc+1]; pc += 2
        if rel & 0x8000: rel -= 65536
        target = (base_addr + pc + rel) & 0xFFFF
        operand = f"${target:04X}"
    elif mode in (PSHS, PULS):
        if pc >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        pb = data[pc]; pc += 1
        operand = psh_pul_list(pb, is_s=True)
    elif mode in (PSHU, PULU):
        if pc >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        pb = data[pc]; pc += 1
        operand = psh_pul_list(pb, is_s=False)
    elif mode in (EXG, TFR):
        if pc >= len(data): return f"{base_addr+start_pc:04X}: ???", pc
        pb = data[pc]; pc += 1
        r1 = REG_NAMES[(pb >> 4) & 0x0F] if (pb >> 4) & 0x0F < len(REG_NAMES) else '?'
        r2 = REG_NAMES[pb & 0x0F] if (pb & 0x0F) < len(REG_NAMES) else '?'
        operand = f"{r1},{r2}"

    raw_bytes = ' '.join(f"{b:02X}" for b in data[start_pc:pc])
    disasm = f"{base_addr+start_pc:04X}: {raw_bytes:<14} {mnem:<6} {operand}"
    return disasm, pc

def main():
    parser = argparse.ArgumentParser(description="MC6809 Disassembler")
    parser.add_argument("file", help="Binary file to disassemble")
    parser.add_argument("--offset", default="0", help="File offset to start disassembly (hex or dec)")
    parser.add_argument("--base", default="0", help="Base address for start of file (hex or dec)")
    parser.add_argument("--count", default="64", help="Number of instructions to disassemble")
    args = parser.parse_args()

    offset = int(args.offset, 0)
    base = int(args.base, 0)
    count = int(args.count, 0)

    with open(args.file, "rb") as f:
        data = f.read()

    pc = offset
    for _ in range(count):
        if pc >= len(data):
            break
        line, pc = disassemble_one(data, pc, base)
        print(line)

if __name__ == "__main__":
    main()
