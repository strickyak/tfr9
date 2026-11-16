#pragma once
// info_port.h -- Firmware diagnostic report port (2 registers: trigger/status + character stream).
//
// Default address in enhancement map `e1`: 0xFF7C (trigger/status), 0xFF7D (stream);
// on `jamesbrown`: 0xFF43..0xFF44.
// Protocol (used by OS-9 `info`):
//   Write 0xA5 to base+0 -> requests core 0 to format a fresh report (`Key\tValue\n` lines).
//   Read  base+0         -> returns 1 once ready, 0 while pending.
//   Read  base+1         -> streams report characters one per read, then 0 at EOF.

static char g_info_text[768];
static volatile uint g_info_len = 0;
static volatile uint g_info_pos = 0;
static volatile bool g_info_request = false;
static volatile bool g_info_ready = false;
static uint64_t g_guest_boot_us = 0;
static bool g_info_active = false;

byte IN_RAM info_read_status(uint) {
  return g_info_ready ? 1 : 0;
}

void IN_RAM info_write_trigger(uint, byte d) {
  if (d == 0xA5) {
    g_info_ready = false;
    __dmb();
    g_info_request = true;
  }
}

byte IN_RAM info_read_stream(uint) {
  if (!g_info_ready || g_info_pos >= g_info_len) return 0;
  uint pos = g_info_pos;
  g_info_pos = pos + 1;
  return (byte)g_info_text[pos];
}

void IN_RAM info_write_stream(uint, byte) {}

inline int info_format_uptime(char* p, int room, uint64_t us) {
  uint32_t t = (uint32_t)(us / 1000000ULL);
  uint32_t d = t / 86400u, h = (t / 3600u) % 24u, m = (t / 60u) % 60u, sec = t % 60u;
  if (d) return snprintf(p, room, "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  if (h) return snprintf(p, room, "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)sec);
  return snprintf(p, room, "%lum %lus", (unsigned long)m, (unsigned long)sec);
}

inline void info_service() {
  if (!g_info_active || !g_info_request) return;
  char* p = g_info_text;
  int room = (int)sizeof(g_info_text);
  int n = 0;
#define INFO_LINE(...)                         \
  do {                                         \
    n = snprintf(p, room, __VA_ARGS__);        \
    if (n > 0 && n < room) { p += n; room -= n; } \
  } while (0)

  char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  pico_get_unique_board_id_string(id, sizeof id);

  const TargetSpec* tgt = nullptr;
  ResolveActiveTarget(g_port_settings, &tgt);

  uint64_t now_us = time_us_64();
  char up_pico[24], up_guest[24];
  info_format_uptime(up_pico, sizeof up_pico, now_us);
  info_format_uptime(up_guest, sizeof up_guest, g_guest_boot_us ? (now_us - g_guest_boot_us) : 0);

  INFO_LINE("Board\tTFR/911H: Hitachi 63C09E + Raspberry Pi RP2350B\n");
  INFO_LINE("Firmware\ttfr9 v5\n");
  if (tgt) {
    INFO_LINE("Target\t%s/%s/%s (engine %s)\n", tgt->on_host, tgt->run_os, tgt->for_hw, tgt->engine);
  }
  INFO_LINE("Pico\tRP2350B at %lu MHz, 520K SRAM\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000));
  INFO_LINE("Pico ID\t%s\n", id);
  if (audio::installed) {
    INFO_LINE("Sound\tSN76489 at 0x%04x (%lu writes), keyclick %s\n",
              (unsigned)audio::port_base, (unsigned long)audio::stat_chip_writes,
              (audio::control & 1) ? "on" : "off");
  }
  if (becker::active) {
    INFO_LINE("FujiNet\tbecker at 0x%04x (%u baud): %lu in, %lu out, %lu lost\n",
              (unsigned)becker::port_base, (unsigned)becker::active_baud,
              (unsigned long)becker::stat_rx_bytes, (unsigned long)becker::stat_tx_bytes,
              (unsigned long)(becker::stat_rx_drops + becker::stat_tx_drops + becker::stat_overruns));
  }
  INFO_LINE("Clock\t%s\n", g_wall_seconds ? "set from PC via tether" : "not set (no tether time yet)");
  INFO_LINE("Uptime\tPico %s, Guest %s\n", up_pico, up_guest);
#undef INFO_LINE

  g_info_len = (uint)(p - g_info_text);
  g_info_pos = 0;
  __dmb();
  g_info_request = false;
  g_info_ready = true;
}

struct InfoPort {
  static constexpr uint16_t kDefaultBase = 0xFF7C;
  static void Install(uint16_t base = kDefaultBase) {
    g_info_len = 0;
    g_info_pos = 0;
    g_info_request = false;
    g_info_ready = false;
    g_info_active = true;
    IoClaim(base, base + 1, "info");
    IoSet(base + 0, info_read_status, info_write_trigger);
    IoSet(base + 1, info_read_stream, info_write_stream);
  }
};
