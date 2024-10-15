#ifndef _BUFFER_H_
#define _BUFFER_H_

//class Buffer {
//    typedef unsigned char byte;
//    typedef unsigned int uint;
//
//    static const uint BLEN = 2 << 10;  // power of 2.
//    static const uint BMASK = BLEN - 1;
//
//    byte buf[BLEN];
//    uint next_in;
//    uint next_out;
//
//public:
//    Buffer()
//        : next_in(0)
//        , next_out(0)
//        {
//            for (uint i = 0; i < BLEN; i++) buf[i]= 0;
//    }
//    uint Mod(uint x) {
//        return BMASK & x;
//    }
//    uint Size() {
//        if (next_out <= next_in) return next_in - next_out;
//        return next_out + BLEN - next_in;
//    }
//    int Peek(uint i) {
//        uint j = Mod(next_out + i);
//        return buf[j];
//    }
//    int Take() {
//        if (Size()) Dump();
//
//        int z = (-1);
//        if (next_in != next_out) {
//            byte z = buf[next_out];
//            next_out = Mod(next_out + 1);
//        }
//        return z;
//    }
//
//    void Dump() {
//        uint in = next_in;
//        uint out = next_out;
//        printf("[[in=%d out=%d size=%d: ", in, out, Size());
//        for (uint i = next_out; i < next_in; i = Mod(i+1)) {
//            printf("%d,", buf[i]);
//        }
//    }
//
//    void Tick() {
//        // putchar(C_GETCHAR);
//        int z = getchar_timeout_us(0);
//        printf("<%d>",z);
//        if (z >= 0) {
//            buf[next_in] = (byte) z;
//            next_in = Mod(next_in + 1);
//        }
//    }
//};

#endif
