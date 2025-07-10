#ifndef _SEEN_H_
#define _SEEN_H_

// Seen remembers what addresses have been "seen"
// as the first byte of an opcode.

// FixedSizeBitmap uses packed bits to remember
// what bits it "contains".
template <uint SZ>
class FixedSizeBitmap {
  byte guts[SZ >> 3];

 public:
  force_inline bool Contains(uint i) const {
    i &= (SZ - 1); // wrap mod SZ
    byte mask = 1 << (i & 7); // which bit in the byte
    uint sub = i >> 3; // which byte
    return (0 != (mask & guts[sub]));
  }
  force_inline void Insert(uint i) {
    i &= (SZ - 1); // wrap mod SZ
    byte mask = 1 << (i & 7); // which bit in the byte
    uint sub = i >> 3; // which byte
    guts[sub] |= mask;
  }
  void Reset() {
    memset(guts, 0, sizeof guts);
  }
};
FixedSizeBitmap<0x10000> SeenBitmap;

template <class T>
struct DontSeen {
  static constexpr bool DoesSeen() { return false; }
  // WasItSeen returns true, so no special treatment for unseen addresses.
  static force_inline bool WasItSeen(uint addr) { return true; }
  static force_inline void SeeIt(uint addr) {}
  static void Reset() {}
};

template <class T>
struct DoSeen {
  static force_inline bool DoesSeen() { return true; }

  static force_inline bool WasItSeen(uint addr) {
    return SeenBitmap.Contains(addr);
  }

  static force_inline void SeeIt(uint addr) {
    SeenBitmap.Insert(addr);
  }

  static void Reset() { SeenBitmap.Reset(); }
};

#endif  // _SEEN_H_
