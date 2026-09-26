#include "cobs_tx.h"
#ifndef _FIRMWARE_PIO_USB_PIPELINE_H_
#define _FIRMWARE_PIO_USB_PIPELINE_H_

#include <string>

#include "../util/circbuf.h"
#include "../util/cobs.h"

extern "C" {
extern int stdio_usb_in_chars(char* buf, int length);
}

extern CircBuf<unsigned char, 1024> usb_raw_buf;
extern CircBuf<std::string*, 64> usb_packet_buf;

class UsbReceiver {
  CircBuf<unsigned char, 1024>& buf_;
  char temp[64];
  int pending = 0;

 public:
  UsbReceiver(CircBuf<unsigned char, 1024>& buf) : buf_(buf) {}

  bool TickHasWork() {
    if (pending > 0) return true;
    int free = 1023 - buf_.NumBuffered();  // Capacity is N-1 = 1023
    if (free > 0) {
      int to_read = free < 64 ? free : 64;
      pending = stdio_usb_in_chars(temp, to_read);
      if (pending > 0) return true;
      if (pending < 0) pending = 0;
    }
    return false;
  }

  void Tick() {
    if (pending <= 0) {
      TickHasWork();
    }
    if (pending > 0) {
      for (int i = 0; i < pending; i++) {
        buf_.Put(temp[i]);
      }
      pending = 0;
    }
  }
};

extern UsbReceiver usb_receiver;
extern CobsDecoder<1024, 64> cobs_decoder;

// #include "script.h"

#define T_COMMAND 179
#define T_RPC 180
#define T_PICO_RPC 181

#if 0
struct CommandEvaluator {
  static bool TickHasWork() {
    return usb_packet_buf.HasAny([](std::string* s) {
      return s && s->length() > 0 && (unsigned char)(*s)[0] == T_COMMAND;
    });
  }

  static void Tick() {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() > 0 && (unsigned char)(*s)[0] == T_COMMAND;
    });
    if (pkt) {
      // Skip T_COMMAND byte
      const char* cmd = pkt->c_str() + 1;

      script::errstring err = script::Eval(cmd, script::global_script_commands);
      if (!err.empty()) {
        cobs_printf("ERROR: <%s>\n", err.c_str());
      }

      delete pkt;
    }
  }
};
#endif

extern void handle_rpc_response(std::string* pkt);

struct RpcEvaluator {
  static bool TickHasWork() {
    return usb_packet_buf.HasAny([](std::string* s) {
      return s && s->length() > 0 && (unsigned char)(*s)[0] == T_RPC;
    });
  }

  static void Tick() {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() > 0 && (unsigned char)(*s)[0] == T_RPC;
    });
    if (pkt) {
      handle_rpc_response(pkt);
      delete pkt;
    }
  }
};

extern void handle_pico_rpc_request(std::string* pkt);

struct PicoRpcEvaluator {
  static bool TickHasWork() {
    return usb_packet_buf.HasAny([](std::string* s) {
      return s && s->length() > 0 && (unsigned char)(*s)[0] == T_PICO_RPC;
    });
  }

  static void Tick() {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() > 0 && (unsigned char)(*s)[0] == T_PICO_RPC;
    });
    if (pkt) {
      handle_pico_rpc_request(pkt);
      delete pkt;
    }
  }
};

inline bool PumpUsbCobsHasWork() {
  return usb_receiver.TickHasWork() || cobs_decoder.TickHasWork() ||
         // CommandEvaluator::TickHasWork() ||
         RpcEvaluator::TickHasWork() || PicoRpcEvaluator::TickHasWork();
}

inline void PumpUsbCobs() {
  usb_receiver.Tick();
  cobs_decoder.Tick();
  // CommandEvaluator::Tick();
  RpcEvaluator::Tick();
  PicoRpcEvaluator::Tick();
}

#endif  // _FIRMWARE_PIO_USB_PIPELINE_H_
