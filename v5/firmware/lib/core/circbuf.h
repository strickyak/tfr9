#ifndef _UTIL_CIRCBUF_H_
#define _UTIL_CIRCBUF_H_

#include <functional>

//#ifndef IN_RAM
//#define IN_RAM
//#endif

using byte = unsigned char;
using uint = unsigned int;

template <typename T, uint N>
class CircBuf {
 private:
  T buf[N];
  uint nextIn, nextOut;

 public:
  void IN_RAM Reset() { nextIn = nextOut = 0; }

  CircBuf() { Reset(); }

  uint IN_RAM NumBuffered() {
    if (nextOut <= nextIn) {
      return nextIn - nextOut;
    } else {
      return N + nextIn - nextOut;
    }
  }

  T IN_RAM Take() {
    T z = buf[nextOut];
    ++nextOut;
    if (nextOut == N) nextOut = 0;
    return z;
  }

  T IN_RAM Yoink(std::function<bool(T)> predicate) {
    if (nextOut == nextIn) return T();

    uint curr = nextOut;
    while (curr != nextIn) {
      if (predicate(buf[curr])) {
        T found = buf[curr];
        uint shift = curr;
        while (shift != nextOut) {
          uint prev = (shift == 0) ? (N - 1) : (shift - 1);
          buf[shift] = buf[prev];
          shift = prev;
        }
        nextOut = (nextOut + 1) % N;
        return found;
      }
      curr = (curr + 1) % N;
    }
    return T();
  }

  bool IN_RAM HasAny(std::function<bool(T)> predicate) {
    if (nextOut == nextIn) return false;
    uint curr = nextOut;
    while (curr != nextIn) {
      if (predicate(buf[curr])) {
        return true;
      }
      curr = (curr + 1) % N;
    }
    return false;
  }

  void IN_RAM Put(T x) {
    buf[nextIn] = x;
    ++nextIn;
    if (nextIn == N) nextIn = 0;
  }
};
#endif  // _UTIL_CIRCBUF_H_
