#ifndef _RAM_INC_H_
#define _RAM_INC_H_

uint quiet_ram;
inline void Quiet() { quiet_ram++; }
inline void Noisy() { quiet_ram--; }

extern uint interest;

#define RP if (interest and not quiet_ram) printf

const byte mmu_init[16] = {

    0, 0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    0, 0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,

    // 0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    // 0x38,0x30,0x31,0x32,0x33,0x3D,0x35,0x3F

    // 0 ,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    // 0 ,0x30,0x31,0x32,0x33,0x3D,0x35,0x3F
};

class SmallRam {
  private:
    byte ram[0x10000];
  public:
    void Reset() {}
    byte Read(uint addr) {
        return ram[addr & 0xFFFF];
    }
    void Write(uint addr, byte data) {
        ram[addr & 0xFFFF] = data;
    }
    byte ReadPhys(uint addr) {
        return ram[addr & 0xFFFF];
    }
    uint PhysSize() { return sizeof ram; }
};

class BigRam {
  private:
    const static uint RAM_SIZE = 128 * 1024;
    const static uint RAM_MASK = RAM_SIZE - 1;

    const static uint SLOT_SHIFT = 13;
    const static uint OFFSET_MASK  = (1 << 13) - 1;
    const static uint SLOT_MASK  = 7;

    byte ram[RAM_SIZE];

#if ENABLE_MMU
    bool enable_mmu;
#else
    constexpr static bool enable_mmu = true;
#endif
    byte current_task;
    uint* current_bases;

    uint base[2][8];
    byte mmu[2][8];

  public:
    void Reset() {
        interest += 500;

#if ENABLE_MMU
        enable_mmu = true;
#endif
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
        RP("RAM_SIZE=$%x=%d. RAM_MASK=$%x=%d. OFFSET_MASK=$%x=%d.\n",
            RAM_SIZE, RAM_SIZE, RAM_MASK, RAM_MASK, OFFSET_MASK, OFFSET_MASK);
    }

    void SetEnableMmu(bool a) {
#if ENABLE_MMU
        if (a != enable_mmu) {
            RP("COCO3: Now MMU is %u\n", a);
        }
        enable_mmu = a;
#endif
    }
    void SetCurrentTask(byte a) {
        assert(a < 2);
        if (a != current_task) {
            RP("COCO3: Now Task is %u\n", a);
        }
        current_task = a;
        current_bases = base[a];
    }
    void WriteMmu(byte task, byte slot, byte blk) {
        RP("WriteMmu: task %x slot %x blk %02x\n",
                task, slot, blk);
        mmu[task][slot] = blk;
        base[task][slot] = (blk << SLOT_SHIFT) & RAM_MASK;
    }
    uint Block(uint addr) {
#define DETERMINE_BLOCK \
        addr = addr & 0xFFFF; \
        uint slot = (addr >> SLOT_SHIFT) & SLOT_MASK; \
        uint offset = addr & OFFSET_MASK; \
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
        uint phys = (basis | offset) & RAM_MASK;

        return phys;
    }
    force_inline uint FastPhys(uint addr, byte block) {
        uint offset = addr & OFFSET_MASK;
        uint basis = current_bases[block];
        uint phys = (basis | offset) & RAM_MASK;

        return phys;
    }
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    force_inline byte Read(uint addr) {
        uint phys = Phys(addr);
        return ram[phys];
    }
    force_inline byte FastRead(uint addr) {
        //uint phys = Phys(addr);
        uint phys2 = FastPhys(addr);
        //if (phys != phys2) {
            //printf("DIFFERENT(%x) %x vs %x\n", addr, phys, phys2);
        //}
        return ram[phys2];
    }
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    force_inline void Write(uint addr, byte data, byte block) {
        uint phys = Phys(addr, block);
        ram[phys] = data;

#if ALL_POKES
                putbyte(C_POKE);
                // putbyte(the_ram.Block(addr));
                putbyte(phys >> 16);
                putbyte(phys >> 8);
                putbyte(phys);
                putbyte(data);
#endif

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
                case 0xAF:
                    {
                        byte task = (addr >> 3) & 1;
                        byte slot = addr & 7;
                        this->WriteMmu(task, slot, data);
                    }
                    break;
            } // switch
        } // if
    } // Write
    void Write(uint addr, byte data) {
        uint phys = Phys(addr);
        ram[phys] = data;

#if ALL_POKES
                putbyte(C_POKE);
                // putbyte(the_ram.Block(addr));
                putbyte(phys >> 16);
                putbyte(phys >> 8);
                putbyte(phys);
                putbyte(data);
#endif

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
                case 0xAF:
                    {
                        byte task = (addr >> 3) & 1;
                        byte slot = addr & 7;
                        this->WriteMmu(task, slot, data);
                    }
                    break;
            } // switch
        } // if
    } // Write
    void FastWrite(uint addr, byte data) {
        uint phys = FastPhys(addr);
        ram[phys] = data;

#if ALL_POKES
                putbyte(C_POKE);
                // putbyte(the_ram.Block(addr));
                putbyte(phys >> 16);
                putbyte(phys >> 8);
                putbyte(phys);
                putbyte(data);
#endif

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
                case 0xAF:
                    {
                        byte task = (addr >> 3) & 1;
                        byte slot = addr & 7;
                        this->WriteMmu(task, slot, data);
                    }
                    break;
            } // switch
        } // if
    } // FastWrite
    byte ReadPhys(uint addr) {
        return ram[addr];
    }
    void WritePhys(uint addr, byte data) {
        ram[addr] = data;
    }
    uint PhysSize() { return sizeof ram; }
};

#if N9_LEVEL == 1
typedef SmallRam Ram;
#endif
#if N9_LEVEL == 2
typedef BigRam Ram;
#endif

Ram the_ram;

force_inline byte FastPeek(uint addr) {
    return the_ram.FastRead(addr);
}
force_inline byte Peek(uint addr) {
    return the_ram.Read(addr);
}

force_inline void Poke(uint addr, byte data) {
    the_ram.Write(addr, data);
}
force_inline void FastPoke(uint addr, byte data) {
    the_ram.FastWrite(addr, data);
}

force_inline void Poke(uint addr, byte data, byte block) {
    the_ram.Write(addr, data, block);
}

force_inline uint Peek2(uint addr) {
    uint hi = Peek(addr);
    uint lo = Peek(addr+1);
    return (hi << 8) | lo;
}
force_inline void Poke2(uint addr, uint data) {
    byte hi = (byte)(data >> 8);
    byte lo = (byte)data;
    Poke(addr, hi);
    Poke(addr+1, lo);
}

#endif
