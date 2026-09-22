#ifndef TURBOLAB_CIRCBUF_H_
#define TURBOLAB_CIRCBUF_H_

#include <cstdint>
#include <functional>

template <typename T, uint32_t N>
class CircBuf {
 private:
  T buf[N];
  uint32_t nextIn, nextOut;

 public:
  void Reset() { nextIn = nextOut = 0; }
  CircBuf() { Reset(); }

  uint32_t NumBuffered() const {
    if (nextOut <= nextIn) {
      return nextIn - nextOut;
    } else {
      return N + nextIn - nextOut;
    }
  }

  bool Empty() const { return nextIn == nextOut; }
  bool Full() const { return NumBuffered() >= N - 1; }

  T Take() {
    T z = buf[nextOut];
    nextOut = (nextOut + 1) % N;
    return z;
  }

  void Put(T x) {
    buf[nextIn] = x;
    nextIn = (nextIn + 1) % N;
  }
};

#endif  // TURBOLAB_CIRCBUF_H_
