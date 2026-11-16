#pragma once
// rtc_port.h -- Real-Time Clock port (8 binary registers: century, year, month, day, wday, hour, min, sec).
//
// Compatible with Jeff Vavasour's CoCo 3 emulator RTC (`clock2_jvemu` / `clock2_tfr911`).
// Default address in enhancement map `e1`: 0xFF72..0xFF79; on `jamesbrown`: 0xFFC0..0xFFC7.
// Wall-clock local time is supplied by `tether` via T_WALLTIME (182: 4-byte big-endian Unix seconds).
// Registers are indexed relative to `g_rtc_base`, so unaligned bases like 0xFF72 work cleanly.

#ifndef T_WALLTIME
#define T_WALLTIME 182
#endif

static uint32_t g_wall_seconds = 0;
static uint64_t g_wall_received_us = 0;
static uint32_t g_rtc_last_refreshed = 0;
static volatile byte g_rtc_regs[8] = {20, 26, 1, 1, 4, 0, 0, 0};
static uint16_t g_rtc_base = 0xFF72;
static bool g_rtc_active = false;

// Pure-integer conversion of local Unix seconds to [century, year, month, day, wday, hour, min, sec].
// Runs on core 0; avoids Newlib gmtime_r/tz locks or flash tables.
inline void unix_to_rtc_fields(uint32_t sec_of_epoch, volatile byte out[8]) {
  int32_t days = (int32_t)(sec_of_epoch / 86400u);
  uint32_t rem = sec_of_epoch % 86400u;
  int wday = (int)((days + 4) % 7);  // 1970-01-01 was Thursday (4); 0 = Sunday
  if (wday < 0) wday += 7;
  int32_t z = days + 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y += 1;
  out[0] = (byte)(y / 100);
  out[1] = (byte)(y % 100);
  out[2] = (byte)m;
  out[3] = (byte)d;
  out[4] = (byte)wday;
  out[5] = (byte)(rem / 3600u);
  out[6] = (byte)((rem / 60u) % 60u);
  out[7] = (byte)(rem % 60u);
}

inline void rtc_refresh_regs(bool force = false) {
  if (!g_wall_seconds) return;
  uint32_t now = g_wall_seconds + (uint32_t)((time_us_64() - g_wall_received_us) / 1000000ULL);
  if (!force && now == g_rtc_last_refreshed) return;
  g_rtc_last_refreshed = now;
  unix_to_rtc_fields(now, g_rtc_regs);
}

// Core 0: pull T_WALLTIME packets from USB before Tcl REPL or PollTermInput sees them.
inline void rtc_poll_walltime() {
  while (true) {
    std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
      return s && s->length() >= 5 && (unsigned char)(*s)[0] == T_WALLTIME;
    });
    if (!pkt) break;
    const unsigned char* p = (const unsigned char*)pkt->data();
    g_wall_seconds = ((uint32_t)p[1] << 24) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 8)  | (uint32_t)p[4];
    g_wall_received_us = time_us_64();
    delete pkt;
    rtc_refresh_regs(/*force=*/true);
  }
}

byte IN_RAM rtc_port_read(uint addr) {
  return g_rtc_regs[(addr - g_rtc_base) & 7];
}

void IN_RAM rtc_port_write(uint, byte) {}

struct RtcPort {
  static constexpr uint16_t kDefaultBase = 0xFF72;
  static void Install(uint16_t base = kDefaultBase) {
    g_rtc_base = base;
    g_rtc_active = true;
    rtc_refresh_regs(/*force=*/true);
    IoClaim(base, base + 7, "rtc");
    for (int i = 0; i < 8; i++) {
      IoSet(base + i, rtc_port_read, rtc_port_write);
    }
  }
};
