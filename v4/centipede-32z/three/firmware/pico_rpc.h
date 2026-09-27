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

extern unsigned char ram[];
#if BECOME_COCO3
constexpr size_t PICO_RAM_SIZE = 128 * 1024;
#else
constexpr size_t PICO_RAM_SIZE = 64 * 1024;
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
  } else if (req.method == "get-ram") {
    resp.status = 0;
    resp.size = PICO_RAM_SIZE;
    if (req.offset >= 0 && static_cast<size_t>(req.offset) < PICO_RAM_SIZE) {
      size_t offset = static_cast<size_t>(req.offset);
      size_t len = (req.length > 0) ? static_cast<size_t>(req.length) : 256;
      if (offset + len > PICO_RAM_SIZE) {
        len = PICO_RAM_SIZE - offset;
      }
      resp.data.assign(reinterpret_cast<const char*>(ram + offset), len);
    } else {
      resp.data.clear();
    }
  } else if (req.method == "put-ram") {
    resp.status = 0;
    resp.size = PICO_RAM_SIZE;
    if (req.offset >= 0 && static_cast<size_t>(req.offset) < PICO_RAM_SIZE) {
      size_t offset = static_cast<size_t>(req.offset);
      size_t len = req.data.size();
      if (offset + len > PICO_RAM_SIZE) {
        len = PICO_RAM_SIZE - offset;
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
  } else if (req.method == "inject") {
    g_pending_injections.push_back(req);
    return; // Do not send response yet. The REPL will handle it.
  } else if (req.method == "get-status") {
    resp.status = 0;
    uint e_hi = 0, e_lo = 0, e_trans = 0;
    bool last_e = gpio_get(G_E);
    for (uint i = 0; i < 2000; i++) {
      bool cur_e = gpio_get(G_E);
      if (cur_e) e_hi++; else e_lo++;
      if (cur_e != last_e) { e_trans++; last_e = cur_e; }
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
             "halt=%d rst=%d e_trans=%u e_hi=%u e_lo=%u f2b_sz=%u active=%d pc=%u/%u cy_idx=%u frozen=%d mmu0=%02x base0=%05x Ty=%d P1=%d",
             gpio_get(G_HALT), gpio_get(G_RESET), e_trans, e_hi, e_lo,
             (unsigned)fg2bg.size(),
             keyboard_injector::active ? 1 : 0,
             (unsigned)keyboard_injector::script_pc,
             (unsigned)keyboard_injector::script_len,
             (unsigned)g_cycle_history_idx,
             g_freeze_cycles ? 1 : 0,
             (unsigned)mmu_task[0][0],
             (unsigned)mmu_base[0],
             SamTyBit ? 1 : 0,
             SamP1Bit ? 1 : 0);
    resp.message = buf;
  } else if (req.method == "get-cycles") {
    resp.status = 0;
    uint8_t cur = g_cycle_history_idx;
    resp.data.resize(256 * 4);
    for (uint i = 0; i < 256; i++) {
      uint8_t slot = (cur + i) & 0xFF;
      resp.data[i * 4 + 0] = g_cycle_history[slot].abus >> 8;
      resp.data[i * 4 + 1] = g_cycle_history[slot].abus & 0xFF;
      resp.data[i * 4 + 2] = g_cycle_history[slot].dbus;
      resp.data[i * 4 + 3] = g_cycle_history[slot].flags;
    }
    if (req.flags & 1) {
      g_freeze_cycles = false;
    }
  } else {
    resp.status = -1;
    resp.message = "unknown PicoRPC method: " + req.method;
  }

  pico_rpc::send_response(resp);
}

#endif  // CENTIPEDE_FIRMWARE_PICO_RPC_H_
