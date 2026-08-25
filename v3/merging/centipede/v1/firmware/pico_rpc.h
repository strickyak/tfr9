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

extern "C" {
extern int putchar_raw(int c);
}

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

// At global scope — matches the extern declaration in usb_pipeline.h.
void handle_pico_rpc_request(std::string* pkt) {
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
    // Echo back the same data the caller sent.
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
    // Send OK response before rebooting (reboot never returns).
    resp.status = 0;
    pico_rpc::send_response(resp);
    sleep_ms(100);  // Give USB time to flush
    rp2350_reset_standard();
    // Never reaches here.
  } else if (req.method == "reflash") {
    // Send OK response before entering BOOTSEL mode (never returns).
    resp.status = 0;
    pico_rpc::send_response(resp);
    sleep_ms(100);  // Give USB time to flush
    rp2350_reset_to_flash_mode();
    // Never reaches here.
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
  } else if (req.method == "inject") {
    g_pending_injections.push_back(req);
    return; // Do not send response yet. The REPL will handle it.
  } else {
    resp.status = -1;
    resp.message = "unknown PicoRPC method: " + req.method;
  }

  pico_rpc::send_response(resp);
}

#endif  // CENTIPEDE_FIRMWARE_PICO_RPC_H_
