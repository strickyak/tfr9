#ifndef _RAM_H_
#define _RAM_H_

uint quiet_ram;
inline void Quiet() { quiet_ram++; }
inline void Noisy() { quiet_ram--; }

extern uint interest;

struct DontTracePokes {
  force_inline void TraceThePoke(uint addr, byte data) {}
};
struct DoTracePokes {
  force_inline void TraceThePoke(uint addr, byte data) {
    putbyte(C_POKE);
    putbyte(addr >> 16);
    putbyte(addr >> 8);
    putbyte(addr);
    putbyte(data);
  }
};

struct DontLogMmu {
  force_inline int Logf(const char* fmt, ...) { return 0; }
};
struct DoLogMmu {
  int Logf(const char* fmt, ...) {
    if (!interest) return 0;
    if (quiet_ram > 0) return 0;

    va_list va;
    va_start(va, fmt);
    int z = printf(fmt, va);
    va_end(va);
    return z;
  }
};

template <class ToLogMmu, class ToTracePokes>
class SmallRam : public ToLogMmu, public ToTracePokes {
 private:
  byte ram[0x10000];

 public:
  void Reset() {}
  byte Read(uint addr) {
    // printf("read %x -> %x\n", addr, ram[addr & 0xFFFF]);
    // TODO -- assert the mask is never needed.
    return ram[addr & 0xFFFF];
  }
  void Write(uint addr, byte data, byte block = 0) {
    // printf("write %x <- %x\n", addr, data);
    // TODO -- assert the mask is never needed.
    ram[addr & 0xFFFF] = data;
    ToTracePokes::TraceThePoke(addr, data);
  }
  byte FastRead(uint addr) { return Read(addr); }
  void FastWrite(uint addr, byte data) { Write(addr, data); }
  byte ReadPhys(uint addr) { return Read(addr); }
  uint PhysSize() { return sizeof ram; }
};

template <class ToLogMmu, class ToTracePokes>
class BigRam : public ToLogMmu, public ToTracePokes {
 private:
  const byte mmu_init[16] = {
      0, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
      0, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
  };

  const static uint RAM_SIZE = 128 * 1024;
  const static uint RAM_MASK = RAM_SIZE - 1;

  const static uint SLOT_SHIFT = 13;
  const static uint OFFSET_MASK = (1 << 13) - 1;
  const static uint SLOT_MASK = 7;

  byte ram[RAM_SIZE];

  bool enable_mmu;
  byte current_task;
  uint* current_bases;

  uint base[2][8];
  byte mmu[2][8];

 public:
  void Reset() {
    interest += 500;

    enable_mmu = true;
    current_task = 1;

    Write(0xFF90, 0x40);
    Write(0xFF91, 0x01);

    // "rel.asm" explains:
    // Coco3 enters this code with TR=1 and the MMU mappings set thus:
    // TR=0: map 0-7: $??,$39,$3A,$3B,$3C,$3D,$3E,$3F
    // TR-1: map 0-7: $38,$30,$31,$32,$33,$3D,$35,$3F
    for (byte i = 0; i < 16; i++) {
      Write(0xFFA0 + i, mmu_init[i]);
    }
    /*
        for (uint task = 0; task < 2; task++) {
            for (uint slot = 0; slot < 8; slot++) {
                mmu[task][slot] = 0x38 + slot;
            }
        }
        */
    ToLogMmu::Logf("RAM_SIZE=$%x=%d. RAM_MASK=$%x=%d. OFFSET_MASK=$%x=%d.\n",
                   RAM_SIZE, RAM_SIZE, RAM_MASK, RAM_MASK, OFFSET_MASK,
                   OFFSET_MASK);
  }

  void SetEnableMmu(bool a) {
    if (a != enable_mmu) {
      ToLogMmu::Logf("COCO3: Now MMU is %u\n", a);
    }
    enable_mmu = a;
  }
  void SetCurrentTask(byte a) {
    assert(a < 2);
    if (a != current_task) {
      ToLogMmu::Logf("COCO3: Now Task is %u\n", a);
    }
    current_task = a;
    current_bases = base[a];
  }
  void WriteMmu(byte task, byte slot, byte blk) {
    ToLogMmu::Logf("WriteMmu: task %x slot %x blk %02x\n", task, slot, blk);
    mmu[task][slot] = blk;
    base[task][slot] = (blk << SLOT_SHIFT) & RAM_MASK;
  }
  uint Block(uint addr) {  // used by the RECORD feature.

#define DETERMINE_BLOCK                         \
  addr = addr & 0xFFFF;                         \
  uint slot = (addr >> SLOT_SHIFT) & SLOT_MASK; \
  uint offset = addr & OFFSET_MASK;             \
  bool use_mmu = enable_mmu && (addr < 0xFE00); \
  uint block = (use_mmu) ? mmu[current_task][slot] : 0x38 + slot;

    DETERMINE_BLOCK
    return block;
  }
  // vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
  force_inline uint Phys(uint addr) {
    DETERMINE_BLOCK

    uint pre_phys = ((block << SLOT_SHIFT) | offset);
    uint phys = ((block << SLOT_SHIFT) | offset) & RAM_MASK;

    return phys;
  }
  force_inline uint Phys(uint addr, byte block) {
    uint offset = addr & OFFSET_MASK;

    uint pre_phys = ((block << SLOT_SHIFT) | offset);
    uint phys = ((block << SLOT_SHIFT) | offset) & RAM_MASK;

    return phys;
  }
  // ==============================================
  force_inline uint FastPhys(uint addr) {
    uint offset = addr & OFFSET_MASK;
    uint slot = (addr >> SLOT_SHIFT) & SLOT_MASK;
    uint basis = current_bases[slot];
    // uint phys = (basis | offset) & RAM_MASK;
    uint phys = basis + offset;

    return phys;
  }
  force_inline uint FastPhys(uint addr, byte block) {
    uint offset = addr & OFFSET_MASK;
    uint basis = current_bases[block];
    // uint phys = (basis | offset) & RAM_MASK;
    uint phys = basis + offset;

    return phys;
  }
  // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  force_inline byte GetPhys(uint phys_addr) { return ram[phys_addr]; }
  force_inline byte Read(uint addr) {
    uint phys = Phys(addr);
    return ram[phys];
  }
  force_inline byte FastRead(uint addr) {
    // uint phys = Phys(addr);
    uint phys2 = FastPhys(addr);
    // if (phys != phys2) {
    // printf("DIFFERENT(%x) %x vs %x\n", addr, phys, phys2);
    //}
    return ram[phys2];
  }
  // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  force_inline void Write(uint addr, byte data, byte block) {
    uint phys = Phys(addr, block);
    ram[phys] = data;

    ToTracePokes::TraceThePoke(phys, data);

    if ((addr & 0xFFC0) == 0xFF80) {
      switch (addr & 0x00FF) {
        case 0x90:
          this->SetEnableMmu((data & 0x40) != 0);
          break;
        case 0x91:
          this->SetCurrentTask(data & 1);
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
          this->WriteMmu(task, slot, data);
        } break;
      }  // switch
    }  // if
  }  // Write
  void Write(uint addr, byte data) {
    uint phys = Phys(addr);
    ram[phys] = data;

    ToTracePokes::TraceThePoke(phys, data);

    if ((addr & 0xFFC0) == 0xFF80) {
      switch (addr & 0x00FF) {
        case 0x90:
          this->SetEnableMmu((data & 0x40) != 0);
          break;
        case 0x91:
          this->SetCurrentTask(data & 1);
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
          this->WriteMmu(task, slot, data);
        } break;
      }  // switch
    }  // if
  }  // Write
  void FastWrite(uint addr, byte data) {
    uint phys = FastPhys(addr);
    ram[phys] = data;

    ToTracePokes::TraceThePoke(phys, data);

    if ((addr & 0xFFC0) == 0xFF80) {
      switch (addr & 0x00FF) {
        case 0x90:
          this->SetEnableMmu((data & 0x40) != 0);
          break;
        case 0x91:
          this->SetCurrentTask(data & 1);
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
          this->WriteMmu(task, slot, data);
        } break;
      }  // switch
    }  // if
  }  // FastWrite
  byte ReadPhys(uint addr) { return ram[addr]; }
  void WritePhys(uint addr, byte data) { ram[addr] = data; }
  uint PhysSize() { return sizeof ram; }
};

#endif  // _RAM_H_
