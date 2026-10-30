// Host test for firmware/lib/core/cobs.h: round-trip payloads of many sizes (including > 1024 bytes,
// which overflowed a stack buffer in v4's firmware/one copy: spec snag S26).
// Build/run:  make test-host   (uses g++ with AddressSanitizer + UBSan)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define IN_RAM
#include "circbuf.h"
#include "cobs.h"

static int failures = 0;

static bool RoundTrip(const std::vector<unsigned char>& payload) {
  std::vector<unsigned char> wire;
  CobsEncodeAndTransmit(payload.data(), payload.size(), [&](int ch) { wire.push_back((unsigned char)ch); });

  static CircBuf<unsigned char, 1024> in;
  static CircBuf<std::string*, 64> out;
  static CobsDecoder<1024, 64> dec(in, out);
  std::string* got = nullptr;
  for (unsigned char b : wire) {
    while (in.NumBuffered() >= 1000) {  // feed in small bursts, like the USB pipeline does
      dec.Tick();
      while (out.NumBuffered() > 0) { delete got; got = out.Take(); }
    }
    in.Put(b);
  }
  dec.Tick();
  while (out.NumBuffered() > 0) { delete got; got = out.Take(); }
  bool ok = got && got->size() == payload.size() &&
            std::equal(payload.begin(), payload.end(), (const unsigned char*)got->data());
  delete got;
  return ok;
}

static void Check(const char* what, size_t len, unsigned seed, int zero_every) {
  std::vector<unsigned char> p(len);
  unsigned x = seed;
  for (size_t i = 0; i < len; i++) {
    x = x * 1103515245u + 12345u;
    unsigned char b = (unsigned char)(x >> 16);
    if (zero_every && i % zero_every == 0) b = 0;
    else if (b == 0) b = 1;
    p[i] = b;
  }
  if (!RoundTrip(p)) { printf("FAIL %s len=%zu\n", what, len); failures++; }
}

int main() {
  for (size_t len = 1; len <= 600; len++) { Check("small/nozero", len, 1, 0); Check("small/zeros", len, 2, 3); }
  static const size_t big[] = {253, 254, 255, 256, 509, 510, 1022, 1023, 1024, 1025, 2047, 2048, 4000, 8192, 20000};
  for (size_t len : big) { Check("big/nozero", len, 3, 0); Check("big/zeros", len, 4, 7); Check("big/allzero", len, 5, 1); }
  if (failures) { printf("cobs_test: %d FAILURES\n", failures); return 1; }
  printf("cobs_test: OK\n");
  return 0;
}
