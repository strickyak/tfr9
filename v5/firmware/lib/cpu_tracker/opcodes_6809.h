#pragma once
#include <cstdint>
#include <cstring>

namespace cputracker {

enum class AddrMode : uint8_t {
  INHERENT = 0,
  IMM8,
  IMM8_EXTRA,      // ANDCC, ORCC, CWAI (3 cycles: opcode, imm, extra fetch)
  IMM16,
  IMM32,           // 6309 LDQ
  DIRECT_R,
  DIRECT_W,
  DIRECT_RW,
  EXTENDED_R,
  EXTENDED_W,
  EXTENDED_RW,
  INDEXED_R,
  INDEXED_W,
  INDEXED_RW,
  INDEXED_LEA,
  REL8,
  REL16,
  BRANCH_ALWAYS_8,
  BRANCH_ALWAYS_16,
  BSR,
  LBSR,
  JSR_DIR,
  JSR_EXT,
  JSR_IND,
  JMP_DIR,
  JMP_EXT,
  JMP_IND,
  PUSHPULL_S,
  PUSHPULL_U,
  EXG_TFR,
  RTS,
  RTI,
  SWI,             // SWI, SWI2, SWI3
  SYNC,
  SETMD,           // 6309 LDMD #imm ($11 3D imm)
  BIT_DIRECT,      // 6309 AIM, OIM, EIM, TIM direct
  BIT_INDEXED,     // 6309 AIM, OIM, EIM, TIM indexed
  BIT_EXTENDED,    // 6309 AIM, OIM, EIM, TIM extended
  TFM,             // 6309 Transfer Memory ($11 38..3B)
  INTER_REG,       // 6309 ADDR, SUBR, CMPR ($10 30..37)
  UNKNOWN
};

struct OpcodeInfo {
  const char* name;
  AddrMode mode;
  uint8_t base_bytes;     // Opcode + immediate/address bytes at PC
  uint8_t data_bytes;     // Data read/write bytes at EA
  bool has_extra_fetch;   // Extra fetch at PC in 6809 Emulation mode
  bool is_write;
  bool valid;
};

class OpcodeTable {
 public:
  OpcodeInfo page1[256];
  OpcodeInfo page2[256];
  OpcodeInfo page3[256];

  OpcodeTable() : page1{}, page2{}, page3{} {
    Init();
  }

  void Init() {
    std::memset(page1, 0, sizeof(page1));
    std::memset(page2, 0, sizeof(page2));
    std::memset(page3, 0, sizeof(page3));

    // =========================================================================
    // Page 1: Motorola 6809 & Hitachi 6309 Page 1 Opcodes
    // =========================================================================

    // Inherent single-byte (1 opcode byte + 1 extra fetch at PC+1 in 6809 mode)
    auto set_inh = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::INHERENT, 1, 0, true, false, true};
    };
    set_inh(0x12, "NOP"); set_inh(0x13, "SYNC"); set_inh(0x19, "DAA"); set_inh(0x1D, "SEX");
    set_inh(0x3A, "ABX"); set_inh(0x3D, "MUL");
    set_inh(0x40, "NEGA"); set_inh(0x43, "COMA"); set_inh(0x44, "LSRA"); set_inh(0x46, "RORA");
    set_inh(0x47, "ASRA"); set_inh(0x48, "ASLA"); set_inh(0x49, "ROLA"); set_inh(0x4A, "DECA");
    set_inh(0x4C, "INCA"); set_inh(0x4D, "TSTA"); set_inh(0x4F, "CLRA");
    set_inh(0x50, "NEGB"); set_inh(0x53, "COMB"); set_inh(0x54, "LSRB"); set_inh(0x56, "RORB");
    set_inh(0x57, "ASRB"); set_inh(0x58, "ASLB"); set_inh(0x59, "ROLB"); set_inh(0x5A, "DECB");
    set_inh(0x5C, "INCB"); set_inh(0x5D, "TSTB"); set_inh(0x5F, "CLRB");

    // 6309 Page 1 inherent additions
    set_inh(0x14, "SEXW"); set_inh(0x4E, "CLRD"); set_inh(0x5E, "CLRW");

    // Special Inherent (RTS, RTI, SWI have extra fetch at PC before stack ops)
    page1[0x39] = {"RTS", AddrMode::RTS, 1, 2, true, false, true};
    page1[0x3B] = {"RTI", AddrMode::RTI, 1, 0, true, false, true};
    page1[0x3F] = {"SWI", AddrMode::SWI, 1, 12, true, true, true};

    // Immediate 8 with Extra Fetch (ANDCC, ORCC, CWAI)
    page1[0x1A] = {"ORCC", AddrMode::IMM8_EXTRA, 2, 0, true, false, true};
    page1[0x1C] = {"ANDCC", AddrMode::IMM8_EXTRA, 2, 0, true, false, true};
    page1[0x3C] = {"CWAI", AddrMode::IMM8_EXTRA, 2, 0, true, false, true};

    // Standard Immediate 8 (2 bytes at PC: opcode + imm8; no extra fetch)
    auto set_imm8 = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::IMM8, 2, 0, false, false, true};
    };
    set_imm8(0x80, "SUBA"); set_imm8(0x81, "CMPA"); set_imm8(0x82, "SBCA"); set_imm8(0x84, "ANDA");
    set_imm8(0x85, "BITA"); set_imm8(0x86, "LDA");  set_imm8(0x88, "EORA"); set_imm8(0x89, "ADCA");
    set_imm8(0x8A, "ORA");  set_imm8(0x8B, "ADDA");
    set_imm8(0xC0, "SUBB"); set_imm8(0xC1, "CMPB"); set_imm8(0xC2, "SBCB"); set_imm8(0xC4, "ANDB");
    set_imm8(0xC5, "BITB"); set_imm8(0xC6, "LDB");  set_imm8(0xC8, "EORB"); set_imm8(0xC9, "ADCB");
    set_imm8(0xCA, "ORB");  set_imm8(0xCB, "ADDB");

    // 6309 Imm8 additions
    set_imm8(0x8F, "LDE"); set_imm8(0xCF, "LDF");

    // Immediate 16 (3 bytes at PC: opcode + imm16; no extra fetch)
    auto set_imm16 = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::IMM16, 3, 0, false, false, true};
    };
    set_imm16(0x83, "SUBD"); set_imm16(0x8C, "CMPX"); set_imm16(0x8E, "LDX");
    set_imm16(0xC3, "ADDD"); set_imm16(0xCC, "LDD");  set_imm16(0xCE, "LDU");

    // 6309 Imm16 & Imm32 additions
    set_imm16(0xC7, "LDW");
    page1[0x87] = {"LDQ", AddrMode::IMM32, 5, 0, false, false, true}; // 32-bit immediate

    // Direct Page
    auto set_dir_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page1[op] = {name, AddrMode::DIRECT_R, 2, dbytes, false, false, true};
    };
    auto set_dir_w = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page1[op] = {name, AddrMode::DIRECT_W, 2, dbytes, false, true, true};
    };
    auto set_dir_rw = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::DIRECT_RW, 2, 2, false, true, true}; // read then write
    };

    set_dir_rw(0x00, "NEG"); set_dir_rw(0x03, "COM"); set_dir_rw(0x04, "LSR"); set_dir_rw(0x06, "ROR");
    set_dir_rw(0x07, "ASR"); set_dir_rw(0x08, "ASL"); set_dir_rw(0x09, "ROL"); set_dir_rw(0x0A, "DEC");
    set_dir_rw(0x0C, "INC"); set_dir_r(0x0D, "TST", 1); set_dir_rw(0x0F, "CLR");
    page1[0x0E] = {"JMP", AddrMode::JMP_DIR, 2, 0, false, false, true};

    // 6309 Direct bit & math operations
    page1[0x01] = {"NEG", AddrMode::DIRECT_RW, 2, 2, false, true, true}; // NEG 16-bit D
    page1[0x02] = {"OIM", AddrMode::BIT_DIRECT, 3, 2, false, true, true}; // imm, dp -> read+write
    page1[0x05] = {"AIM", AddrMode::BIT_DIRECT, 3, 2, false, true, true};
    page1[0x0B] = {"TIM", AddrMode::BIT_DIRECT, 3, 1, false, false, true};

    set_dir_r(0x90, "SUBA", 1); set_dir_r(0x91, "CMPA", 1); set_dir_r(0x92, "SBCA", 1); set_dir_r(0x93, "SUBD", 2);
    set_dir_r(0x94, "ANDA", 1); set_dir_r(0x95, "BITA", 1); set_dir_r(0x96, "LDA", 1); set_dir_w(0x97, "STA", 1);
    set_dir_r(0x98, "EORA", 1); set_dir_r(0x99, "ADCA", 1); set_dir_r(0x9A, "ORA", 1); set_dir_r(0x9B, "ADDA", 1);
    set_dir_r(0x9C, "CMPX", 2); page1[0x9D] = {"JSR", AddrMode::JSR_DIR, 2, 3, false, true, true};
    set_dir_r(0x9E, "LDX", 2); set_dir_w(0x9F, "STX", 2);

    set_dir_r(0xD0, "SUBB", 1); set_dir_r(0xD1, "CMPB", 1); set_dir_r(0xD2, "SBCB", 1); set_dir_r(0xD3, "ADDD", 2);
    set_dir_r(0xD4, "ANDB", 1); set_dir_r(0xD5, "BITB", 1); set_dir_r(0xD6, "LDB", 1); set_dir_w(0xD7, "STB", 1);
    set_dir_r(0xD8, "EORB", 1); set_dir_r(0xD9, "ADCB", 1); set_dir_r(0xDA, "ORB", 1); set_dir_r(0xDB, "ADDB", 1);
    set_dir_r(0xDC, "LDD", 2); set_dir_w(0xDD, "STD", 2); set_dir_r(0xDE, "LDU", 2); set_dir_w(0xDF, "STU", 2);

    // Extended
    auto set_ext_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page1[op] = {name, AddrMode::EXTENDED_R, 3, dbytes, false, false, true};
    };
    auto set_ext_w = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page1[op] = {name, AddrMode::EXTENDED_W, 3, dbytes, false, true, true};
    };
    auto set_ext_rw = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::EXTENDED_RW, 3, 2, false, true, true};
    };

    set_ext_rw(0x70, "NEG"); set_ext_rw(0x73, "COM"); set_ext_rw(0x74, "LSR"); set_ext_rw(0x76, "ROR");
    set_ext_rw(0x77, "ASR"); set_ext_rw(0x78, "ASL"); set_ext_rw(0x79, "ROL"); set_ext_rw(0x7A, "DEC");
    set_ext_rw(0x7C, "INC"); set_ext_r(0x7D, "TST", 1); set_ext_rw(0x7F, "CLR");
    page1[0x7E] = {"JMP", AddrMode::JMP_EXT, 3, 0, false, false, true};

    // 6309 Extended bit operations
    page1[0x71] = {"COM", AddrMode::EXTENDED_RW, 3, 2, false, true, true};
    page1[0x72] = {"OIM", AddrMode::BIT_EXTENDED, 4, 2, false, true, true};
    page1[0x75] = {"AIM", AddrMode::BIT_EXTENDED, 4, 2, false, true, true};
    page1[0x7B] = {"TIM", AddrMode::BIT_EXTENDED, 4, 1, false, false, true};

    set_ext_r(0xB0, "SUBA", 1); set_ext_r(0xB1, "CMPA", 1); set_ext_r(0xB2, "SBCA", 1); set_ext_r(0xB3, "SUBD", 2);
    set_ext_r(0xB4, "ANDA", 1); set_ext_r(0xB5, "BITA", 1); set_ext_r(0xB6, "LDA", 1); set_ext_w(0xB7, "STA", 1);
    set_ext_r(0xB8, "EORA", 1); set_ext_r(0xB9, "ADCA", 1); set_ext_r(0xBA, "ORA", 1); set_ext_r(0xBB, "ADDA", 1);
    set_ext_r(0xBC, "CMPX", 2); page1[0xBD] = {"JSR", AddrMode::JSR_EXT, 3, 3, false, true, true};
    set_ext_r(0xBE, "LDX", 2); set_ext_w(0xBF, "STX", 2);

    set_ext_r(0xF0, "SUBB", 1); set_ext_r(0xF1, "CMPB", 1); set_ext_r(0xF2, "SBCB", 1); set_ext_r(0xF3, "ADDD", 2);
    set_ext_r(0xF4, "ANDB", 1); set_ext_r(0xF5, "BITB", 1); set_ext_r(0xF6, "LDB", 1); set_ext_w(0xF7, "STB", 1);
    set_ext_r(0xF8, "EORB", 1); set_ext_r(0xF9, "ADCB", 1); set_ext_r(0xFA, "ORB", 1); set_ext_r(0xFB, "ADDB", 1);
    set_ext_r(0xFC, "LDD", 2); set_ext_w(0xFD, "STD", 2); set_ext_r(0xFE, "LDU", 2); set_ext_w(0xFF, "STU", 2);

    // Indexed
    auto set_idx_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page1[op] = {name, AddrMode::INDEXED_R, 2, dbytes, false, false, true};
    };
    auto set_idx_w = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page1[op] = {name, AddrMode::INDEXED_W, 2, dbytes, false, true, true};
    };
    auto set_idx_rw = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::INDEXED_RW, 2, 2, false, true, true};
    };

    set_idx_rw(0x60, "NEG"); set_idx_rw(0x63, "COM"); set_idx_rw(0x64, "LSR"); set_idx_rw(0x66, "ROR");
    set_idx_rw(0x67, "ASR"); set_idx_rw(0x68, "ASL"); set_idx_rw(0x69, "ROL"); set_idx_rw(0x6A, "DEC");
    set_idx_rw(0x6C, "INC"); set_idx_r(0x6D, "TST", 1); set_idx_rw(0x6F, "CLR");
    page1[0x6E] = {"JMP", AddrMode::JMP_IND, 2, 0, false, false, true};

    // 6309 Indexed bit operations
    page1[0x61] = {"COM", AddrMode::INDEXED_RW, 2, 2, false, true, true};
    page1[0x62] = {"OIM", AddrMode::BIT_INDEXED, 3, 2, false, true, true};
    page1[0x65] = {"AIM", AddrMode::BIT_INDEXED, 3, 2, false, true, true};
    page1[0x6B] = {"TIM", AddrMode::BIT_INDEXED, 3, 1, false, false, true};

    auto set_lea = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::INDEXED_LEA, 2, 0, false, false, true};
    };
    set_lea(0x30, "LEAX"); set_lea(0x31, "LEAY"); set_lea(0x32, "LEAS"); set_lea(0x33, "LEAU");

    set_idx_r(0xA0, "SUBA", 1); set_idx_r(0xA1, "CMPA", 1); set_idx_r(0xA2, "SBCA", 1); set_idx_r(0xA3, "SUBD", 2);
    set_idx_r(0xA4, "ANDA", 1); set_idx_r(0xA5, "BITA", 1); set_idx_r(0xA6, "LDA", 1); set_idx_w(0xA7, "STA", 1);
    set_idx_r(0xA8, "EORA", 1); set_idx_r(0xA9, "ADCA", 1); set_idx_r(0xAA, "ORA", 1); set_idx_r(0xAB, "ADDA", 1);
    set_idx_r(0xAC, "CMPX", 2); page1[0xAD] = {"JSR", AddrMode::JSR_IND, 2, 3, false, true, true};
    set_idx_r(0xAE, "LDX", 2); set_idx_w(0xAF, "STX", 2);

    set_idx_r(0xE0, "SUBB", 1); set_idx_r(0xE1, "CMPB", 1); set_idx_r(0xE2, "SBCB", 1); set_idx_r(0xE3, "ADDD", 2);
    set_idx_r(0xE4, "ANDB", 1); set_idx_r(0xE5, "BITB", 1); set_idx_r(0xE6, "LDB", 1); set_idx_w(0xE7, "STB", 1);
    set_idx_r(0xE8, "EORB", 1); set_idx_r(0xE9, "ADCB", 1); set_idx_r(0xEA, "ORB", 1); set_idx_r(0xEB, "ADDB", 1);
    set_idx_r(0xEC, "LDD", 2); set_idx_w(0xED, "STD", 2); set_idx_r(0xEE, "LDU", 2); set_idx_w(0xEF, "STU", 2);

    // Relative 8-bit branches (2 bytes at PC; no extra fetch in visible log)
    auto set_rel8 = [this](uint8_t op, const char* name) {
      page1[op] = {name, AddrMode::REL8, 2, 0, false, false, true};
    };
    page1[0x20] = {"BRA", AddrMode::BRANCH_ALWAYS_8, 2, 0, false, false, true};
    page1[0x8D] = {"BSR", AddrMode::BSR, 2, 2, false, true, true}; // 2 stack writes
    set_rel8(0x21, "BRN"); set_rel8(0x22, "BHI"); set_rel8(0x23, "BLS"); set_rel8(0x24, "BCC");
    set_rel8(0x25, "BCS"); set_rel8(0x26, "BNE"); set_rel8(0x27, "BEQ"); set_rel8(0x28, "BVC");
    set_rel8(0x29, "BVS"); set_rel8(0x2A, "BPL"); set_rel8(0x2B, "BMI"); set_rel8(0x2C, "BGE");
    set_rel8(0x2D, "BLT"); set_rel8(0x2E, "BGT"); set_rel8(0x2F, "BLE");

    // Relative 16-bit branches
    page1[0x16] = {"LBRA", AddrMode::BRANCH_ALWAYS_16, 3, 0, false, false, true};
    page1[0x17] = {"LBSR", AddrMode::LBSR, 3, 2, false, true, true};

    // Push / Pull / Exg / Tfr
    page1[0x34] = {"PSHS", AddrMode::PUSHPULL_S, 2, 0, false, true, true};
    page1[0x35] = {"PULS", AddrMode::PUSHPULL_S, 2, 0, false, false, true};
    page1[0x36] = {"PSHU", AddrMode::PUSHPULL_U, 2, 0, false, true, true};
    page1[0x37] = {"PULU", AddrMode::PUSHPULL_U, 2, 0, false, false, true};
    page1[0x1E] = {"EXG", AddrMode::EXG_TFR, 2, 0, false, false, true};
    page1[0x1F] = {"TFR", AddrMode::EXG_TFR, 2, 0, false, false, true};

    // =========================================================================
    // Page 2: Prefix 0x10
    // =========================================================================
    auto set_rel16_p2 = [this](uint8_t op, const char* name) {
      page2[op] = {name, AddrMode::REL16, 4, 0, false, false, true}; // prefix(1) + op(1) + rel16(2)
    };
    page2[0x20] = {"LBRA", AddrMode::BRANCH_ALWAYS_16, 4, 0, false, false, true};
    set_rel16_p2(0x21, "LBRN"); set_rel16_p2(0x22, "LBHI"); set_rel16_p2(0x23, "LBLS"); set_rel16_p2(0x24, "LBCC");
    set_rel16_p2(0x25, "LBCS"); set_rel16_p2(0x26, "LBNE"); set_rel16_p2(0x27, "LBEQ"); set_rel16_p2(0x28, "LBVC");
    set_rel16_p2(0x29, "LBVS"); set_rel16_p2(0x2A, "LBPL"); set_rel16_p2(0x2B, "LBMI"); set_rel16_p2(0x2C, "LBGE");
    set_rel16_p2(0x2D, "LBLT"); set_rel16_p2(0x2E, "LBGT"); set_rel16_p2(0x2F, "LBLE");

    page2[0x3F] = {"SWI2", AddrMode::SWI, 2, 12, true, true, true};

    // Page 2 Immediate 16
    auto set_imm16_p2 = [this](uint8_t op, const char* name) {
      page2[op] = {name, AddrMode::IMM16, 4, 0, false, false, true};
    };
    set_imm16_p2(0x83, "CMPD"); set_imm16_p2(0x8C, "CMPY"); set_imm16_p2(0x8E, "LDY"); set_imm16_p2(0xCE, "LDS");

    // Page 2 Direct
    auto set_dir_p2_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page2[op] = {name, AddrMode::DIRECT_R, 3, dbytes, false, false, true};
    };
    auto set_dir_p2_w = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page2[op] = {name, AddrMode::DIRECT_W, 3, dbytes, false, true, true};
    };
    set_dir_p2_r(0x93, "CMPD", 2); set_dir_p2_r(0x9C, "CMPY", 2); set_dir_p2_r(0x9E, "LDY", 2); set_dir_p2_w(0x9F, "STY", 2);
    set_dir_p2_r(0xDE, "LDS", 2);  set_dir_p2_w(0xDF, "STS", 2);

    // Page 2 Indexed
    auto set_idx_p2_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page2[op] = {name, AddrMode::INDEXED_R, 3, dbytes, false, false, true};
    };
    auto set_idx_p2_w = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page2[op] = {name, AddrMode::INDEXED_W, 3, dbytes, false, true, true};
    };
    set_idx_p2_r(0xA3, "CMPD", 2); set_idx_p2_r(0xAC, "CMPY", 2); set_idx_p2_r(0xAE, "LDY", 2); set_idx_p2_w(0xAF, "STY", 2);
    set_idx_p2_r(0xEE, "LDS", 2);  set_idx_p2_w(0xEF, "STS", 2);

    // Page 2 Extended
    auto set_ext_p2_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page2[op] = {name, AddrMode::EXTENDED_R, 4, dbytes, false, false, true};
    };
    auto set_ext_p2_w = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page2[op] = {name, AddrMode::EXTENDED_W, 4, dbytes, false, true, true};
    };
    set_ext_p2_r(0xB3, "CMPD", 2); set_ext_p2_r(0xBC, "CMPY", 2); set_ext_p2_r(0xBE, "LDY", 2); set_ext_p2_w(0xBF, "STY", 2);
    set_ext_p2_r(0xFE, "LDS", 2);  set_ext_p2_w(0xFF, "STS", 2);

    // 6309 Page 2 inter-register instructions ($10 30..37)
    auto set_inter_reg = [this](uint8_t op, const char* name) {
      page2[op] = {name, AddrMode::INTER_REG, 3, 0, false, false, true}; // prefix + op + postbyte
    };
    set_inter_reg(0x30, "ADDR"); set_inter_reg(0x31, "ADCR"); set_inter_reg(0x32, "SUBR");
    set_inter_reg(0x33, "SBCR"); set_inter_reg(0x34, "ANDR"); set_inter_reg(0x35, "ORR");
    set_inter_reg(0x36, "EORR"); set_inter_reg(0x37, "CMPR");

    // 6309 Page 2 stack W pushes/pulls
    page2[0x38] = {"PSHSW", AddrMode::PUSHPULL_S, 2, 2, false, true, true};
    page2[0x39] = {"PULSW", AddrMode::PUSHPULL_S, 2, 2, false, false, true};
    page2[0x3A] = {"PSHUW", AddrMode::PUSHPULL_U, 2, 2, false, true, true};
    page2[0x3B] = {"PULUW", AddrMode::PUSHPULL_U, 2, 2, false, false, true};

    // =========================================================================
    // Page 3: Prefix 0x11
    // =========================================================================
    page3[0x3F] = {"SWI3", AddrMode::SWI, 2, 12, true, true, true};

    // Page 3 Immediate 16
    auto set_imm16_p3 = [this](uint8_t op, const char* name) {
      page3[op] = {name, AddrMode::IMM16, 4, 0, false, false, true};
    };
    set_imm16_p3(0x83, "CMPU"); set_imm16_p3(0x8C, "CMPS");

    // 6309 Mode Register Set ($11 3D imm)
    page3[0x3D] = {"LDMD", AddrMode::SETMD, 3, 0, false, false, true}; // prefix + op + imm8

    // 6309 Block Transfer ($11 38..3B)
    page3[0x38] = {"TFM", AddrMode::TFM, 3, 0, false, false, true};
    page3[0x39] = {"TFM", AddrMode::TFM, 3, 0, false, false, true};
    page3[0x3A] = {"TFM", AddrMode::TFM, 3, 0, false, false, true};
    page3[0x3B] = {"TFM", AddrMode::TFM, 3, 0, false, false, true};

    // Page 3 Direct
    auto set_dir_p3_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page3[op] = {name, AddrMode::DIRECT_R, 3, dbytes, false, false, true};
    };
    set_dir_p3_r(0x93, "CMPU", 2); set_dir_p3_r(0x9C, "CMPS", 2);

    // Page 3 Indexed
    auto set_idx_p3_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page3[op] = {name, AddrMode::INDEXED_R, 3, dbytes, false, false, true};
    };
    set_idx_p3_r(0xA3, "CMPU", 2); set_idx_p3_r(0xAC, "CMPS", 2);

    // Page 3 Extended
    auto set_ext_p3_r = [this](uint8_t op, const char* name, uint8_t dbytes) {
      page3[op] = {name, AddrMode::EXTENDED_R, 4, dbytes, false, false, true};
    };
    set_ext_p3_r(0xB3, "CMPU", 2); set_ext_p3_r(0xBC, "CMPS", 2);
  }
};

} // namespace cputracker
