#ifndef _UTIL_COBS_H_
#define _UTIL_COBS_H_

#include <string>

#include "circbuf.h"

#ifndef IN_RAM
#define IN_RAM
#endif

#ifndef COBS_CHECKSUMS
#define COBS_CHECKSUMS 1
#endif

// Compute one's complement checksum: ~(sum of bytes) & 0xFF
inline unsigned char CobsChecksum(const unsigned char* data, size_t len) {
  unsigned char sum = 0;
  for (size_t i = 0; i < len; i++) sum += data[i];
  return (~sum) & 0xFF;
}

template <uint IN_BUF_LEN, uint OUT_BUF_LEN>
class CobsDecoder {
 private:
  CircBuf<unsigned char, IN_BUF_LEN>& in_buf_;
  CircBuf<std::string*, OUT_BUF_LEN>& out_buf_;

  std::string current_packet_;
  int code_;
  int copy_len_;
  bool expecting_code_;

  void IN_RAM ProcessByte(unsigned char b) {
    if (b == 0) {
      // 0x00 is the frame delimiter.
      if (expecting_code_ && !current_packet_.empty()) {
        // We reached the end of the packet cleanly.
        if (code_ < 0xFF) {
          // The last block appended an implicit zero, but since it's the end
          // of the packet, this zero should not be part of the payload.
          current_packet_.pop_back();
        }

        // If the packet has data, emit it.
        if (!current_packet_.empty()) {
#if COBS_CHECKSUMS
          // Verify checksum (last byte) and strip it.
          size_t plen = current_packet_.size();
          if (plen >= 2) {
            unsigned char sum = 0;
            for (size_t i = 0; i < plen; i++) sum += (unsigned char)current_packet_[i];
            if ((sum & 0xFF) == 0xFF) {
              current_packet_.pop_back();  // Strip checksum byte
              out_buf_.Put(new std::string(current_packet_));
            }
            // else: checksum mismatch, discard packet silently
          }
#else
          out_buf_.Put(new std::string(current_packet_));
#endif
        }
      }

      // If expecting_code_ was false, we hit 0x00 prematurely in the middle
      // of copying data (a framing error). The packet is discarded cleanly.

      // Reset state for the next packet.
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

      // If we just finished a block (either copy_len became 0, or was 0 to
      // begin with)
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

  bool IN_RAM TickHasWork() {
    return in_buf_.NumBuffered() > 0 &&
           out_buf_.NumBuffered() < (OUT_BUF_LEN - 1);
  }

  void IN_RAM Tick() {
    while (in_buf_.NumBuffered() > 0) {
      // Apply backpressure if the output buffer is full.
      // For a CircBuf of size OUT_BUF_LEN, the maximum capacity is
      // OUT_BUF_LEN-1.
      if (out_buf_.NumBuffered() >= OUT_BUF_LEN - 1) {
        break;  // Stop taking input to avoid dropping packets.
      }

      unsigned char b = in_buf_.Take();
      ProcessByte(b);
    }
  }
};

// CobsEncodeAndTransmit performs COBS encoding on a payload and transmits it
// using the provided putc function. It prepends and appends a framing zero.
template <typename Func>
inline void CobsEncodeAndTransmit(const unsigned char* data, size_t len, Func putc_func) {
#if COBS_CHECKSUMS
  // Build payload + checksum in a temp buffer
  unsigned char ckbuf[1024];
  for (size_t i = 0; i < len && i < sizeof(ckbuf) - 1; i++) ckbuf[i] = data[i];
  ckbuf[len] = CobsChecksum(data, len);
  const unsigned char* payload = ckbuf;
  size_t payload_len = len + 1;
#else
  const unsigned char* payload = data;
  size_t payload_len = len;
#endif

  putc_func(0); // Leading frame delimiter: kills any partial packet in receiver
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
      // If that zero was the last byte, emit an empty block (code=1)
      // so the decoder appends the implicit zero for it.
      if (ptr == payload_len) {
        putc_func(1);
      }
    }
  }
  putc_func(0); // Frame delimiter
}

#endif  // _UTIL_COBS_H_
