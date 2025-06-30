#ifndef _HYPER_H_
#define _HYPER_H_

template <typename T>
struct DontHyper {
    static force_inline void Hyper(uint current_opcode, uint pc) {
    }
};

template <typename T>
struct DoHyper {
    constexpr static byte BRN = 0x21; 
    constexpr static byte NOP = 0x12; 

    static void Hyper(uint current_opcode, uint pc) {
       if (current_opcode != BRN) return;
       if (T::Peek(pc-1) != NOP) return;
       byte hyper = T::Peek(pc+1);
       char buf[20];
       sprintf(buf, " [Hyper:%d] ", hyper);
       ShowStr(buf);
       printf(" [Hyper:%d] ", hyper);
    }
};

#endif // _HYPER_H_
