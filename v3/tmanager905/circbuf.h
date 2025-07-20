#ifndef _CIRCBUF_H_
#define _CIRCBUF_H_

template <uint N>
class CircBuf {
 private:
  byte buf[N];
  uint nextIn, nextOut;

  uint NumBytesAvailable() {
    if (nextOut <= nextIn)
      return nextIn - nextOut;
    else
      return nextIn + N - nextOut;
  }

 public:
  static constexpr uint MASK = N - 1;
  CircBuf() : nextIn(0), nextOut(0) {}

  bool HasAtLeast(uint n) {
    uint ba = NumBytesAvailable();
    return ba >= n;
  }
  byte Peek(uint i = 0) {
    return buf[(nextOut+i)&MASK];
  }
  byte Take() {
    byte z = buf[nextOut];
    ++nextOut;
    if (nextOut >= N) nextOut = 0;
    return z;
  }
  void Put(byte x) {
    // ShowChar('`');
    buf[nextIn] = x;
    ++nextIn;
    if (nextIn >= N) nextIn = 0;
  }
};
CircBuf<1024> usb_input;
CircBuf<1024> term_input;
CircBuf<1024> disk_input;

#endif  // _CIRCBUF_H_
