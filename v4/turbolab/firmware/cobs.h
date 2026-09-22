#ifndef TURBOLAB_COBS_H_
#define TURBOLAB_COBS_H_

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "circbuf.h"

#ifndef COBS_CHECKSUMS
#define COBS_CHECKSUMS 1
#endif

extern "C" {
extern int putchar_raw(int c);
extern bool stdio_usb_connected(void);
}

inline unsigned char CobsChecksum(const unsigned char* data, size_t len) {
  unsigned char sum = 0;
  for (size_t i = 0; i < len; i++) sum += data[i];
  return (~sum) & 0xFF;
}

template <typename Func>
inline void CobsEncodeAndTransmit(const unsigned char* data, size_t len, Func putc_func) {
#if COBS_CHECKSUMS
  unsigned char ckbuf[2048];
  for (size_t i = 0; i < len && i < sizeof(ckbuf) - 1; i++) ckbuf[i] = data[i];
  ckbuf[len] = CobsChecksum(data, len);
  const unsigned char* payload = ckbuf;
  size_t payload_len = len + 1;
#else
  const unsigned char* payload = data;
  size_t payload_len = len;
#endif

  putc_func(0);  // Leading frame delimiter
  size_t ptr = 0;
  while (ptr < payload_len) {
    size_t dist = 1;
    while (dist < 255 && ptr + dist - 1 < payload_len && payload[ptr + dist - 1] != 0) {
      dist++;
    }

    putc_func(dist);
    for (size_t i = 1; i < dist; i++) {
      putc_func(payload[ptr + i - 1]);
    }
    ptr += dist - 1;
    if (ptr < payload_len && payload[ptr] == 0) {
      ptr++;
      if (ptr == payload_len) {
        putc_func(1);
      }
    }
  }
  putc_func(0);  // Trailing frame delimiter
}

template <uint32_t IN_BUF_LEN, uint32_t OUT_BUF_LEN>
class CobsDecoder {
 private:
  CircBuf<unsigned char, IN_BUF_LEN>& in_buf_;
  CircBuf<std::string*, OUT_BUF_LEN>& out_buf_;

  std::string current_packet_;
  int code_;
  int copy_len_;
  bool expecting_code_;

  void ProcessByte(unsigned char b) {
    if (b == 0) {
      if (expecting_code_ && !current_packet_.empty()) {
        if (code_ < 0xFF) {
          current_packet_.pop_back();
        }

        if (!current_packet_.empty()) {
#if COBS_CHECKSUMS
          size_t plen = current_packet_.size();
          if (plen >= 2) {
            unsigned char sum = 0;
            for (size_t i = 0; i < plen; i++) sum += (unsigned char)current_packet_[i];
            if ((sum & 0xFF) == 0xFF) {
              current_packet_.pop_back();  // Strip checksum byte
              out_buf_.Put(new std::string(current_packet_));
            }
          }
#else
          out_buf_.Put(new std::string(current_packet_));
#endif
        }
      }
      current_packet_.clear();
      expecting_code_ = true;
      code_ = 0;
      copy_len_ = 0;
    } else {
      if (expecting_code_) {
        code_ = b;
        copy_len_ = code_ - 1;
        expecting_code_ = (copy_len_ == 0);
      } else {
        current_packet_.push_back(b);
        copy_len_--;
        if (copy_len_ == 0) {
          expecting_code_ = true;
        }
      }

      if (expecting_code_) {
        if (code_ < 0xFF) {
          current_packet_.push_back(0);
        }
      }
    }
  }

 public:
  CobsDecoder(CircBuf<unsigned char, IN_BUF_LEN>& in_buf,
              CircBuf<std::string*, OUT_BUF_LEN>& out_buf)
      : in_buf_(in_buf),
        out_buf_(out_buf),
        code_(0),
        copy_len_(0),
        expecting_code_(true) {}

  void Tick() {
    while (in_buf_.NumBuffered() > 0) {
      if (out_buf_.NumBuffered() >= OUT_BUF_LEN - 1) break;
      unsigned char b = in_buf_.Take();
      ProcessByte(b);
    }
  }
};

#endif  // TURBOLAB_COBS_H_
