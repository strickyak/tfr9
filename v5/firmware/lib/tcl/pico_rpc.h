#ifndef CENTIPEDE_FIRMWARE_PICO_RPC_H_
#define CENTIPEDE_FIRMWARE_PICO_RPC_H_

// PicoRPC: Tether sends requests, firmware executes, firmware sends responses.
// This is the reverse direction of VFS RPC (where firmware sends requests
// and tether executes).
//
// Packet format (both directions):
//   [T_PICO_RPC, pcb-encoded RpcRequest or RpcResponse...]
//
// Runs inline in PumpUsbCobs on Core 0's scheduler loop.
// Only simple, non-blocking handlers should be added here.

#include "pcb.h"
#include "cobs_tx.h"
#include "keyboard_injector.h"

extern "C" {
extern int putchar_raw(int c);
}

extern unsigned char ram[256 * 1024];
#ifndef PICO_RPC_RAM_SIZE
#define PICO_RPC_RAM_SIZE (256 * 1024)
#endif

namespace pico_rpc {

inline void send_response(const pcb::RpcResponse& resp) {
  std::vector<uint8_t> payload = resp.encode();
  unsigned char* pkt = new unsigned char[payload.size() + 1];
  pkt[0] = T_PICO_RPC;
  for (size_t i = 0; i < payload.size(); i++) {
    pkt[i + 1] = payload[i];
  }
  CobsEncodeAndTransmit(pkt, payload.size() + 1, putchar_raw);
  delete[] pkt;
}

}  // namespace pico_rpc

inline std::vector<pcb::RpcRequest> g_pending_injections;

// Optional product-specific RPC handler (e.g. Centipede's get-status / get-cycles).
inline bool (*g_product_rpc_hook)(const pcb::RpcRequest& req, pcb::RpcResponse& resp) = nullptr;

// At global scope — matches the extern declaration in usb_pipeline.h.
inline void handle_pico_rpc_request(std::string* pkt) {
  if (!pkt || pkt->length() < 2) return;

  // Skip T_PICO_RPC byte
  std::vector<uint8_t> buf;
  buf.reserve(pkt->length() - 1);
  for (size_t i = 1; i < pkt->length(); i++) {
    buf.push_back(static_cast<uint8_t>((*pkt)[i]));
  }

  pcb::RpcRequest req = pcb::RpcRequest::decode(buf);
  pcb::RpcResponse resp;
  resp.serial = req.serial;

  if (req.method == "ping") {
    resp.status = 0;
    resp.data = req.data;
  } else if (req.method == "restart") {
    if (req.data.size() == 4) {
      uint32_t mode = ((uint32_t)req.data[0] << 24) |
                      ((uint32_t)req.data[1] << 16) |
                      ((uint32_t)req.data[2] << 8) |
                      (uint32_t)req.data[3];
      ::boot_mode = mode;
      ::boot_mode_check = mode + BOOT_MODE_CHECKER;
    }
    resp.status = 0;
    pico_rpc::send_response(resp);
    sleep_ms(100);  // Give USB time to flush
    rp2350_reset_standard();
  } else if (req.method == "reflash") {
    resp.status = 0;
    pico_rpc::send_response(resp);
    sleep_ms(100);  // Give USB time to flush
    rp2350_reset_to_flash_mode();
  } else if (req.method == "reformat") {
    lfs_unmount(&lfs_volume);
    lfs_format(&lfs_volume, &lfs);
    int err = lfs_mount(&lfs_volume, &lfs);
    if (err) {
      resp.status = -1;
      resp.message = "error mounting after format";
    } else {
      resp.status = 0;
    }
  } else if (req.method == "get-ram") {
    resp.status = 0;
    resp.size = PICO_RPC_RAM_SIZE;
    if (req.offset >= 0 && static_cast<size_t>(req.offset) < PICO_RPC_RAM_SIZE) {
      size_t offset = static_cast<size_t>(req.offset);
      size_t len = (req.length > 0) ? static_cast<size_t>(req.length) : 256;
      if (offset + len > PICO_RPC_RAM_SIZE) {
        len = PICO_RPC_RAM_SIZE - offset;
      }
      resp.data.assign(reinterpret_cast<const char*>(ram + offset), len);
    } else {
      resp.data.clear();
    }
  } else if (req.method == "put-ram") {
    resp.status = 0;
    resp.size = PICO_RPC_RAM_SIZE;
    if (req.offset >= 0 && static_cast<size_t>(req.offset) < PICO_RPC_RAM_SIZE) {
      size_t offset = static_cast<size_t>(req.offset);
      size_t len = req.data.size();
      if (offset + len > PICO_RPC_RAM_SIZE) {
        len = PICO_RPC_RAM_SIZE - offset;
      }
      memcpy(ram + offset, req.data.data(), len);
      resp.size = len;
    } else {
      resp.status = -1;
      resp.message = "offset out of range";
    }
  } else if (req.method == "type") {
    uint16_t down_ticks = (req.length > 0) ? (uint16_t)req.length : TYPING_DOWN_TICKS;
    uint16_t up_ticks = (req.flags > 0) ? (uint16_t)req.flags : TYPING_UP_TICKS;
    keyboard_injector::queue_string(req.data, down_ticks, up_ticks);
    keyboard_injector::start_if_queued();
    resp.status = 0;
  } else if (req.method == "get-label") {
    resp.status = 0;
    resp.data = FlashLabel::RawLabel();
  } else if (req.method == "set-label") {
    std::string err_msg;
    if (gspoon::g_spoon_coro == nullptr) {
      resp.status = -1;
      resp.message = "set-label requires Phase 1 (TCL>); run -just-restart=27 first";
    } else if (!FlashLabel::ValidateRawLabel(req.data, &err_msg)) {
      resp.status = -1;
      resp.message = err_msg;
    } else {
      uint8_t page[FlashLabel::LABEL_MAX_LEN];
      memset(page, 0, sizeof(page));
      memcpy(page, req.data.data(), req.data.size());
      pico_flash_write_label(page);
      FlashLabel::InitLabel();
      FlashLabel::PopulateTclArray(global_tcl_interp);
      std::string readback = FlashLabel::RawLabel();
      if (readback != req.data) {
        resp.status = -1;
        resp.message = "flash label readback mismatch";
      } else {
        resp.status = 0;
        resp.data = readback;
      }
    }
  } else if (req.method == "inject") {
    g_pending_injections.push_back(req);
    return;  // REPL / menu will send the response after evaluating.
  } else if (g_product_rpc_hook && g_product_rpc_hook(req, resp)) {
    // Handled by product hook.
  } else {
    resp.status = -1;
    resp.message = "unknown PicoRPC method: " + req.method;
  }

  pico_rpc::send_response(resp);
}

#endif  // CENTIPEDE_FIRMWARE_PICO_RPC_H_
