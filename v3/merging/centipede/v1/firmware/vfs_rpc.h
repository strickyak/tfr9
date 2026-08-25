#ifndef FIRMWARE_PIO_VFS_RPC_H_
#define FIRMWARE_PIO_VFS_RPC_H_

#include <string>
#include <vector>

#include "coro.h"
#include "pcb.h"

#define T_RPC 180

extern void PumpUsbCobs();

extern "C" {
extern int putchar_raw(int c);
}

#include "abort.h"
#include "cobs_tx.h"

namespace rpc {

// Global coroutine handle for VFS RPC calls.
// Set by BackgroundSpoonFeeder before calling Tcl_Eval, so that all
// VFS operations invoked by Tcl commands can yield instead of
// busy-polling PumpUsbCobs on the coroutine's small (4K) stack.
// See coro.h for stack budget analysis.
inline Coro* g_vfs_coro = nullptr;

inline void send_rpc(const pcb::RpcRequest& req) {
#if RPC_VERBOSE
  cobs_printf("{r%d:%s ", req.serial, req.method.c_str());
#endif
  std::vector<uint8_t> payload = req.encode();
  unsigned char* pkt = new unsigned char[payload.size() + 1];
  pkt[0] = T_RPC;
  for (size_t i = 0; i < payload.size(); i++) {
    pkt[i + 1] = payload[i];
  }
  CobsEncodeAndTransmit(pkt, payload.size() + 1, putchar_raw);
  delete[] pkt;
}

volatile bool rpc_response_ready = false;
pcb::RpcResponse last_rpc_response;
int next_serial = 1;
volatile bool vfs_rpc_busy = false;
}  // namespace rpc

// At global scope — matches the extern declaration in usb_pipeline.h.
void handle_rpc_response(std::string* pkt) {
  if (!pkt || pkt->length() < 2) return;

  // Skip T_RPC byte
  std::vector<uint8_t> buf;
  buf.reserve(pkt->length() - 1);
  for (size_t i = 1; i < pkt->length(); i++) {
    buf.push_back(static_cast<uint8_t>((*pkt)[i]));
  }

  rpc::last_rpc_response = pcb::RpcResponse::decode(buf);
  rpc::rpc_response_ready = true;
#if RPC_VERBOSE
  cobs_printf(" r%d,", rpc::last_rpc_response.serial);
#endif
}

namespace rpc {

inline pcb::RpcResponse vfs_rpc_call(const pcb::RpcRequest& req, Coro* self = nullptr) {
  if (!usb_tether_ok()) {
    // No USB tether: cannot perform RPC to host.
    // Return an error response so the VFS layer propagates the failure.
    pcb::RpcResponse err_resp;
    err_resp.status = -1;
    err_resp.serial = req.serial;
    return err_resp;
  }

  // Use the explicit self if provided, otherwise fall back to the global
  // coroutine handle.  Must have one or the other — we can't busy-poll
  // PumpUsbCobs on a 4K coroutine stack.
  Coro* coro = self ? self : g_vfs_coro;
  CENTIPEDE_ASSERT(coro, "vfs_rpc: no Coro");

  while (vfs_rpc_busy) {
    coro_yield(coro);
  }
  vfs_rpc_busy = true;

  rpc_response_ready = false;
  send_rpc(req);

  // Block until we get a response.
  // Yielding lets the scheduler pump USB on its main (large) stack,
  // avoiding deep stack usage on 4K coroutine stacks.
  while (!rpc_response_ready) {
    coro_yield(coro);
  }

#if RPC_VERBOSE
  cobs_printf(" r%d}", req.serial);
#endif
  vfs_rpc_busy = false;
  return last_rpc_response;
}

}  // namespace rpc

#endif  // FIRMWARE_PIO_VFS_RPC_H_
