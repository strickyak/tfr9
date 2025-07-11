#ifndef _RAM_H_
#define _RAM_H_

const static uint BIG_RAM_SIZE = 128 * 1024;
const static uint BIG_RAM_MASK = BIG_RAM_SIZE - 1;

byte ram[BIG_RAM_SIZE];

bool enable_mmu;
byte current_task;
uint* current_bases;

uint base[2][8];
byte mmu[2][8];

template <typename T>
struct DontTracePokes {
  force_inline static void TraceThePoke(uint addr, byte data) {}
};
template <typename T>
struct DoTracePokes {
  force_inline static void TraceThePoke(uint addr, byte data) {
    putbyte(C_POKE);
    putbyte(addr >> 16);
    putbyte(addr >> 8);
    putbyte(addr);
    putbyte(data);
  }
};

template <typename T>
struct DontLogMmu {
  static force_inline void LogMmuf(const char* fmt, ...) {}
};
template <typename T>
struct DoLogMmu {
  static void LogMmuf(const char* fmt, ...) {
    // if (!interest) return;
    if (quiet_ram > 0) return;

    va_list va;
    va_start(va, fmt);
    int z = vprintf(fmt, va);
    va_end(va);
  }
};

template <typename T>
struct CommonRam {
  static force_inline byte FastPeek(uint addr) { return T::FastRead(addr); }
  static force_inline byte Peek(uint addr) { return T::Read(addr); }

  static force_inline void Poke(uint addr, byte data) {
    T::Write(addr, data);
    // printf("PPOOKKEE %x <- %x\n", addr, data);
  }
#if 0
force_inline void PokeQuietly(uint addr, byte data) { T::WriteQuietly(addr, data); }
#endif
  static force_inline void FastPoke(uint addr, byte data) {
    T::FastWrite(addr, data);
  }

  static force_inline void Poke(uint addr, byte data, byte block) {
    T::Write(addr, data, block);
  }

  static force_inline uint Peek2(uint addr) {
    uint hi = Peek(addr);
    uint lo = Peek(addr + 1);
    return (hi << 8) | lo;
  }
  static force_inline void Poke2(uint addr, uint data) {
    byte hi = (byte)(data >> 8);
    byte lo = (byte)data;
    Poke(addr, hi);
    Poke(addr + 1, lo);
  }
};

template <typename T>
class SmallRam {
 public:
  static void ResetRam() { memset(ram, 0, sizeof ram); }
  static byte Read(uint addr) {
    // printf("read %x -> %x\n", addr, ram[addr & 0xFFFF]);
    // TODO -- assert the mask is never needed.
    return ram[addr & 0xFFFF];
  }
  static void Write(uint addr, byte data, byte block = 0) {
    // printf("write %x <- %x\n", addr, data);
    // TODO -- assert the mask is never needed.
    ram[addr & 0xFFFF] = data;
    T::TraceThePoke(addr, data);
  }
  static byte FastRead(uint addr) { return Read(addr); }
  static void FastWrite(uint addr, byte data) { Write(addr, data); }
  static byte ReadPhys(uint addr) { return Read(addr); }
  static uint PhysSize() { return sizeof ram; }
};

static byte const BigRam_mmu_init[16] = {
    0, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    // 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    // 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

template <typename T>
class BigRam {
 private:
  const static uint SLOT_SHIFT = 13;
  const static uint OFFSET_MASK = (1 << 13) - 1;
  const static uint SLOT_MASK = 7;

 public:
  static void ResetRam() {
    printf("ResetRam\n");
    memset(ram, 0, sizeof ram);
    interest = (interest > 500) ? interest : 500;

    enable_mmu = true;
    current_task = 1;

    Write(0xFF90, 0x40);
    Write(0xFF91, 0x01);

    // "rel.asm" explains:
    // Coco3 enters this code with TR=1 and the MMU mappings set thus:
    // TR=0: map 0-7: $??,$39,$3A,$3B,$3C,$3D,$3E,$3F
    // TR-1: map 0-7: $38,$30,$31,$32,$33,$3D,$35,$3F
    for (byte i = 0; i < 16; i++) {
      Write(0xFFA0 + i, BigRam_mmu_init[i]);
    }
    /*
        for (uint task = 0; task < 2; task++) {
            for (uint slot = 0; slot < 8; slot++) {
                mmu[task][slot] = 0x38 + slot;
            }
        }
        */
    T::LogMmuf(
        "BIG_RAM_SIZE=$%x=%d. BIG_RAM_MASK=$%x=%d. OFFSET_MASK=$%x=%d.\n",
        BIG_RAM_SIZE, BIG_RAM_SIZE, BIG_RAM_MASK, BIG_RAM_MASK, OFFSET_MASK,
        OFFSET_MASK);
  }

  static void SetEnableMmu(bool a) {
    if (a != enable_mmu) {
      T::LogMmuf("MMU: Now MMU is %u\n", a);
    }
    enable_mmu = a;
  }
  static void SetCurrentTask(byte a) {
    assert(a < 2);
    if (a != current_task) {
      T::LogMmuf("MMU: Now Task is %u\n", a);
    }
    current_task = a;
    current_bases = base[a];
  }
  static void WriteMmu(byte task, byte slot, byte blk) {
    T::LogMmuf("WriteMmu: task %x slot %x blk %02x\n", task, slot, blk);
    mmu[task][slot] = blk;
    base[task][slot] = (blk << SLOT_SHIFT) & BIG_RAM_MASK;
  }
  static uint Block(uint addr) {  // used by the RECORD feature.

#define DETERMINE_BLOCK                         \
  addr = addr & 0xFFFF;                         \
  uint slot = (addr >> SLOT_SHIFT) & SLOT_MASK; \
  uint offset = addr & OFFSET_MASK;             \
  bool use_mmu = enable_mmu && (addr < 0xFE00); \
  uint block = (use_mmu) ? mmu[current_task][slot] : 0x38 + slot;

    DETERMINE_BLOCK
    // printf("DET addr=%x slot=%x off=%x use=%x/%x block=%x\n",
    // addr, slot, offset, use_mmu, current_task, block);
    return block;
  }
  // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
  force_inline static uint Phys(uint addr) {
    DETERMINE_BLOCK

    uint pre_phys = ((block << SLOT_SHIFT) | offset);
    uint phys = ((block << SLOT_SHIFT) | offset) & BIG_RAM_MASK;

    // T::LogMmuf("Phys: %x -> %x\n", addr, phys);
    return phys;
  }
  force_inline static uint Phys(uint addr, byte block) {
    uint offset = addr & OFFSET_MASK;

    uint pre_phys = ((block << SLOT_SHIFT) | offset);
    uint phys = ((block << SLOT_SHIFT) | offset) & BIG_RAM_MASK;

    // T::LogMmuf("Phys: %x(%x) -> %x\n", addr, block, phys);
    return phys;
  }
  // ==============================================
  force_inline static uint FastPhys(uint addr) {
    uint offset = addr & OFFSET_MASK;
    uint slot = (addr >> SLOT_SHIFT) & SLOT_MASK;
    uint basis = current_bases[slot];
    // uint phys = (basis | offset) & BIG_RAM_MASK;
    uint phys = basis + offset;

    return phys;
  }
  force_inline static uint FastPhys(uint addr, byte block) {
    uint offset = addr & OFFSET_MASK;
    uint basis = current_bases[block];
    // uint phys = (basis | offset) & BIG_RAM_MASK;
    uint phys = basis + offset;

    return phys;
  }
  // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  force_inline static byte GetPhys(uint phys_addr) { return ram[phys_addr]; }
  force_inline static byte Read(uint addr) {
    uint phys = Phys(addr);
    // T::LogMmuf("read: %x %x -> %x\n", addr, phys, ram[phys]);
    return ram[phys];
  }
  force_inline static byte FastRead(uint addr) {
    // uint phys = Phys(addr);
    uint phys2 = FastPhys(addr);
    // if (phys != phys2) {
    // printf("DIFFERENT(%x) %x vs %x\n", addr, phys, phys2);
    //}
    // T::LogMmuf("fastread: %x %x -> %x\n", addr, phys2, ram[phys2]);
    return ram[phys2];
  }
  // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  force_inline static void Write(uint addr, byte data, byte block) {
    uint phys = Phys(addr, block);
    ram[phys] = data;

    // T::LogMmuf("write: %x(%x) %x <- %x\n", addr, block, phys, data);
    T::TraceThePoke(phys, data);

    if ((addr & 0xFFC0) == 0xFF80) {
      switch (addr & 0x00FF) {
        case 0x90:
          SetEnableMmu((data & 0x40) != 0);
          break;
        case 0x91:
          SetCurrentTask(data & 1);
          break;
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
        case 0xA4:
        case 0xA5:
        case 0xA6:
        case 0xA7:
        case 0xA8:
        case 0xA9:
        case 0xAA:
        case 0xAB:
        case 0xAC:
        case 0xAD:
        case 0xAE:
        case 0xAF: {
          byte task = (addr >> 3) & 1;
          byte slot = addr & 7;
          WriteMmu(task, slot, data);
        } break;
      }  // switch
    }  // if
  }  // Write
  static void Write(uint addr, byte data) {
    // printf("write: %x() <- %x\n", addr, data);
    uint phys = Phys(addr);
    ram[phys] = data;

    // printf("write: %x() %x <- %x\n", addr, phys, data);
    T::TraceThePoke(phys, data);

    if ((addr & 0xFFC0) == 0xFF80) {
      switch (addr & 0x00FF) {
        case 0x90:
          SetEnableMmu((data & 0x40) != 0);
          break;
        case 0x91:
          SetCurrentTask(data & 1);
          break;
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
        case 0xA4:
        case 0xA5:
        case 0xA6:
        case 0xA7:
        case 0xA8:
        case 0xA9:
        case 0xAA:
        case 0xAB:
        case 0xAC:
        case 0xAD:
        case 0xAE:
        case 0xAF: {
          byte task = (addr >> 3) & 1;
          byte slot = addr & 7;
          WriteMmu(task, slot, data);
        } break;
      }  // switch
    }  // if
  }  // Write
  static void FastWrite(uint addr, byte data) {
    uint phys = FastPhys(addr);
    ram[phys] = data;

    // T::LogMmuf("fastwrite: %x() %x <- %x\n", addr, phys, data);
    T::TraceThePoke(phys, data);

    if ((addr & 0xFFC0) == 0xFF80) {
      switch (addr & 0x00FF) {
        case 0x90:
          SetEnableMmu((data & 0x40) != 0);
          break;
        case 0x91:
          SetCurrentTask(data & 1);
          break;
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
        case 0xA4:
        case 0xA5:
        case 0xA6:
        case 0xA7:
        case 0xA8:
        case 0xA9:
        case 0xAA:
        case 0xAB:
        case 0xAC:
        case 0xAD:
        case 0xAE:
        case 0xAF: {
          byte task = (addr >> 3) & 1;
          byte slot = addr & 7;
          WriteMmu(task, slot, data);
        } break;
      }  // switch
    }  // if
  }  // FastWrite
  static byte ReadPhys(uint addr) { return ram[addr]; }
  static void WritePhys(uint addr, byte data) { ram[addr] = data; }
  static uint PhysSize() { return sizeof ram; }

  ///////// Peek & Poke

  ///////// end Peek & Poke
};

#endif  // _RAM_H_
