#ifndef TURBOLAB_PCB_H_
#define TURBOLAB_PCB_H_

#include <cstdint>
#include <string>
#include <vector>

// Proto-Crawl Buffers for RPCs

namespace pcb {

constexpr uint8_t KIND_INT = 1;
constexpr uint8_t KIND_STR = 2;

inline uint8_t make_tag(uint8_t field_num, uint8_t kind) {
  return (field_num << 3) | kind;
}

inline void put_varint(std::vector<uint8_t>& buf, uint64_t val) {
  while (val >= 0x80) {
    buf.push_back((val & 0x7F) | 0x80);
    val >>= 7;
  }
  buf.push_back(val & 0x7F);
}

inline uint64_t get_varint(const std::vector<uint8_t>& buf, size_t& offset) {
  uint64_t val = 0;
  int shift = 0;
  while (offset < buf.size()) {
    uint8_t b = buf[offset++];
    val |= (uint64_t)(b & 0x7F) << shift;
    if (!(b & 0x80)) break;
    shift += 7;
  }
  return val;
}

inline void put_int(std::vector<uint8_t>& buf, uint8_t field_num, int64_t val) {
  buf.push_back(make_tag(field_num, KIND_INT));
  put_varint(buf, static_cast<uint64_t>(val));
}

inline void put_str(std::vector<uint8_t>& buf, uint8_t field_num,
                    const std::string& str) {
  buf.push_back(make_tag(field_num, KIND_STR));
  put_varint(buf, str.length());
  for (char c : str) {
    buf.push_back(c);
  }
}

inline void put_bytes(std::vector<uint8_t>& buf, uint8_t field_num,
                      const uint8_t* data, size_t len) {
  buf.push_back(make_tag(field_num, KIND_STR));
  put_varint(buf, len);
  for (size_t i = 0; i < len; i++) {
    buf.push_back(data[i]);
  }
}

struct RpcRequest {
  std::string method;
  int serial = 0;
  std::string path;
  int flags = 0;
  int length = 0;
  std::string data;
  int64_t offset = 0;
  int64_t whence = 0;

  static RpcRequest decode(const std::vector<uint8_t>& buf) {
    RpcRequest req;
    size_t offset = 0;
    while (offset < buf.size()) {
      uint8_t tag = buf[offset++];
      if (tag == 0) break;

      uint8_t field_num = tag >> 3;
      uint8_t kind = tag & 7;

      if (kind == KIND_INT) {
        int64_t val = static_cast<int64_t>(get_varint(buf, offset));
        switch (field_num) {
          case 2: req.serial = val; break;
          case 6: req.flags = val; break;
          case 7: req.length = val; break;
          case 9: req.offset = val; break;
          case 10: req.whence = val; break;
        }
      } else if (kind == KIND_STR) {
        size_t len = get_varint(buf, offset);
        std::string s;
        s.reserve(len);
        for (size_t i = 0; i < len && offset < buf.size(); i++) {
          s.push_back(buf[offset++]);
        }
        switch (field_num) {
          case 1: req.method = s; break;
          case 3: req.path = s; break;
          case 8: req.data = s; break;
        }
      }
    }
    return req;
  }
};

struct RpcResponse {
  int status = 0;
  std::string message;
  int serial = 0;
  std::string data;

  std::vector<uint8_t> encode() const {
    std::vector<uint8_t> buf;
    if (status != 0) put_int(buf, 1, status);
    if (!message.empty()) put_str(buf, 1, message);
    if (serial != 0) put_int(buf, 2, serial);
    if (!data.empty()) put_str(buf, 4, data);
    buf.push_back(0);  // Terminator
    return buf;
  }
};

}  // namespace pcb

#endif  // TURBOLAB_PCB_H_
