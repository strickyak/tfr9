#include "vfs_rpc.h"

#include <vector>

namespace rpc {
bool rpc_response_ready = false;
pcb::RpcResponse last_rpc_response;
int next_serial = 1;
}  // namespace rpc

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
