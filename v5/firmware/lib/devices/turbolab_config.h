#pragma once
// turbolab_config.h -- Pure C++17 parser, validator, and OS-9 module symbol resolver
// for `on tfr911h run turbolab with ...` options.  No Pico SDK, no heap: compiles on host & Pico.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace turbolab {

// Trace flag bitmasks (matches wire protocol and tether TraceFormatter)
constexpr uint8_t kTraceX    = 1 << 0;  // 'x': FIC opcode fetch
constexpr uint8_t kTraceR    = 1 << 1;  // 'r': data read
constexpr uint8_t kTraceW    = 1 << 2;  // 'w': data write
constexpr uint8_t kTraceI    = 1 << 3;  // 'i': interrupt vectors, RTI, SWI2 syscalls
constexpr uint8_t kTraceT    = 1 << 4;  // 't': OS-9 traps
constexpr uint8_t kTracePlus = 1 << 5;  // '+': opcode continuation / operand bytes
constexpr uint8_t kTraceIdle = 1 << 6;  // '-': internal/idle bus cycles
constexpr uint8_t kTraceAllNoIdle = kTraceX | kTracePlus | kTraceR | kTraceW | kTraceI | kTraceT;

constexpr int kMaxWatches = 16;

inline char ToLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline bool StrEqIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (ToLowerAscii(*a++) != ToLowerAscii(*b++)) return false;
  }
  return *a == 0 && *b == 0;
}

inline bool IsTurbolabOptionKey(const char* key) {
  if (!key) return false;
  if (!strcmp(key, "trigger") || !strcmp(key, "trace_count") || !strcmp(key, "trace") ||
      !strcmp(key, "stop") || !strcmp(key, "max_cycles") || !strcmp(key, "max_time") ||
      !strcmp(key, "watch") || !strcmp(key, "image"))
    return true;
  if (!strncmp(key, "watch.", 6) && key[6] >= '0' && key[6] <= '9') return true;
  return false;
}

// Parse count with optional hex ($/0x) and SI (k/m/g = 10^3/6/9) or binary (K/M/G = 2^10/20/30) suffix.
inline bool ParseCount(const char* s, uint64_t* out) {
  if (!s || !*s) return false;
  size_t len = strlen(s);
  uint64_t mult = 1;
  char last = s[len - 1];
  bool hex_mode = (s[0] == '$') || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
  // Only treat trailing letter as a suffix if not a hex digit in hex mode (note: k/K/m/M/g/G are > 'f' anyway!).
  if (last == 'k') { mult = 1000ULL; len--; }
  else if (last == 'K') { mult = 1024ULL; len--; }
  else if (last == 'm') { mult = 1000000ULL; len--; }
  else if (last == 'M') { mult = 1024ULL * 1024ULL; len--; }
  else if (last == 'g') { mult = 1000000000ULL; len--; }
  else if (last == 'G') { mult = 1024ULL * 1024ULL * 1024ULL; len--; }

  if (len == 0) return false;
  size_t pos = 0;
  int base = 10;
  if (hex_mode) {
    base = 16;
    pos = (s[0] == '$') ? 1 : 2;
    if (pos >= len) return false;
  }
  uint64_t val = 0;
  for (; pos < len; pos++) {
    char c = s[pos];
    int d = (c >= '0' && c <= '9') ? (c - '0')
          : (base == 16 && c >= 'a' && c <= 'f') ? (c - 'a' + 10)
          : (base == 16 && c >= 'A' && c <= 'F') ? (c - 'A' + 10)
          : -1;
    if (d < 0) return false;
    if (val > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) return false;
    val = val * (uint64_t)base + (uint64_t)d;
  }
  if (val == 0 || val > UINT64_MAX / mult) return false;
  *out = val * mult;
  return true;
}

// Parse duration into microseconds: `<N>us`, `<N>ms`, `<N>[.<frac>]s`, `<N>[.<frac>]m`,
// or bare `<N>[.<frac>]` seconds when `allow_bare_seconds` is true.
inline bool ParseDurationUs(const char* s, uint64_t* out_us, bool allow_bare_seconds) {
  if (!s || !*s) return false;
  size_t len = strlen(s);
  uint64_t scale_us = 0;
  if (len > 2 && ToLowerAscii(s[len - 2]) == 'u' && ToLowerAscii(s[len - 1]) == 's') {
    scale_us = 1ULL; len -= 2;
  } else if (len > 2 && ToLowerAscii(s[len - 2]) == 'm' && ToLowerAscii(s[len - 1]) == 's') {
    scale_us = 1000ULL; len -= 2;
  } else if (len > 1 && ToLowerAscii(s[len - 1]) == 's') {
    scale_us = 1000000ULL; len -= 1;
  } else if (len > 1 && ToLowerAscii(s[len - 1]) == 'm') {
    scale_us = 60ULL * 1000000ULL; len -= 1;
  } else if (allow_bare_seconds) {
    scale_us = 1000000ULL;
  } else {
    return false;
  }
  if (len == 0) return false;

  uint64_t whole = 0;
  size_t i = 0;
  bool any_digit = false;
  while (i < len && s[i] != '.') {
    if (s[i] < '0' || s[i] > '9') return false;
    any_digit = true;
    whole = whole * 10 + (uint64_t)(s[i] - '0');
    i++;
  }
  uint64_t frac_num = 0;
  uint64_t frac_den = 1;
  if (i < len && s[i] == '.') {
    if (scale_us < 1000ULL) return false;  // no fractional us
    i++;
    for (; i < len; i++) {
      if (s[i] < '0' || s[i] > '9') return false;
      any_digit = true;
      if (frac_den < 1000000ULL) {
        frac_num = frac_num * 10 + (uint64_t)(s[i] - '0');
        frac_den *= 10;
      }
    }
  }
  if (!any_digit) return false;
  uint64_t us = whole * scale_us + (frac_num * scale_us) / frac_den;
  if (us == 0) return false;
  *out_us = us;
  return true;
}

// Parse trace flags: `0`/`none`, `1`/`all`, `idle`/`-`, or characters from `1x+rwit-` (slash-separated allowed).
inline bool ParseTraceFlags(const char* s, uint8_t* out) {
  if (!s || !*s) return false;
  if (StrEqIgnoreCase(s, "0") || StrEqIgnoreCase(s, "none")) { *out = 0; return true; }
  uint8_t mask = 0;
  const char* p = s;
  while (*p) {
    const char* end = p;
    while (*end && *end != '/') end++;
    size_t slen = (size_t)(end - p);
    if (slen == 0) return false;
    if (slen == 3 && ToLowerAscii(p[0]) == 'a' && ToLowerAscii(p[1]) == 'l' && ToLowerAscii(p[2]) == 'l') {
      mask |= kTraceAllNoIdle;
    } else if (slen == 4 && ToLowerAscii(p[0]) == 'i' && ToLowerAscii(p[1]) == 'd' &&
               ToLowerAscii(p[2]) == 'l' && ToLowerAscii(p[3]) == 'e') {
      mask |= kTraceIdle;
    } else {
      for (size_t i = 0; i < slen; i++) {
        switch (ToLowerAscii(p[i])) {
          case '1': mask |= kTraceAllNoIdle; break;
          case 'x': mask |= kTraceX; break;
          case '+': mask |= kTracePlus; break;
          case 'r': mask |= kTraceR | kTraceX | kTraceI | kTracePlus; break;
          case 'w': mask |= kTraceW; break;
          case 'i': mask |= kTraceI; break;
          case 't': mask |= kTraceT; break;
          case '-': mask |= kTraceIdle; break;
          default: return false;
        }
      }
    }
    p = (*end == '/') ? end + 1 : end;
  }
  *out = mask;
  return true;
}

// Parse explicit 16-bit hex address (`0x1234` or `$1234`).
inline bool ParseHexAddr16(const char* s, uint16_t* out) {
  if (!s) return false;
  if (s[0] == '$') s++;
  else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  else return false;
  if (!*s) return false;
  uint32_t v = 0;
  for (; *s; s++) {
    int d = (*s >= '0' && *s <= '9') ? (*s - '0')
          : (*s >= 'a' && *s <= 'f') ? (*s - 'a' + 10)
          : (*s >= 'A' && *s <= 'F') ? (*s - 'A' + 10)
          : -1;
    if (d < 0) return false;
    v = (v << 4) | (uint32_t)d;
    if (v > 0xFFFFu) return false;
  }
  *out = (uint16_t)v;
  return true;
}

// Parse module symbol offset: `$hex`, `0xhex`, hex with [a-fA-F], or decimal.
inline bool ParseModOffset(const char* s, uint16_t* out) {
  if (!s || !*s) return false;
  if (!strcmp(s, "0")) { *out = 0; return true; }
  if (s[0] == '$' || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
    return ParseHexAddr16(s, out);
  }
  bool has_hex = false;
  for (const char* p = s; *p; p++) {
    if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) { has_hex = true; break; }
  }
  uint32_t v = 0;
  int base = has_hex ? 16 : 10;
  for (const char* p = s; *p; p++) {
    char c = *p;
    int d = (c >= '0' && c <= '9') ? (c - '0')
          : (base == 16 && c >= 'a' && c <= 'f') ? (c - 'a' + 10)
          : (base == 16 && c >= 'A' && c <= 'F') ? (c - 'A' + 10)
          : -1;
    if (d < 0) return false;
    v = v * (uint32_t)base + (uint32_t)d;
    if (v > 0xFFFFu) return false;
  }
  *out = (uint16_t)v;
  return true;
}

struct WatchSpec {
  char type = 0;         // 0 = any ('r'|'w'|'x'), 'r', 'w', 'x'
  bool symbolic = false; // true when target is `@module[+|-offset]`
  uint16_t addr = 0;
  char mod_name[32] = {};
  int32_t mod_offset = 0;
  uint32_t count = 0;    // 0 = every hit (logging watch), >=1 = Nth hit
};

inline bool ParseAddrTarget(const char* expr, WatchSpec* out) {
  if (!expr || !*expr) return false;
  if (expr[0] == '@') {
    const char* p = expr + 1;
    const char* sign_pos = nullptr;
    for (const char* q = p; *q; q++) {
      if (*q == '+' || *q == '-') { sign_pos = q; break; }
    }
    size_t nlen = sign_pos ? (size_t)(sign_pos - p) : strlen(p);
    if (nlen == 0 || nlen >= sizeof(out->mod_name)) return false;
    memcpy(out->mod_name, p, nlen);
    out->mod_name[nlen] = 0;
    if (nlen > 5 && StrEqIgnoreCase(out->mod_name + nlen - 5, ".list")) {
      out->mod_name[nlen - 5] = 0;
    }
    out->mod_offset = 0;
    if (sign_pos) {
      uint16_t off = 0;
      if (!ParseModOffset(sign_pos + 1, &off)) return false;
      out->mod_offset = (*sign_pos == '-') ? -(int32_t)off : (int32_t)off;
    }
    out->symbolic = true;
    return true;
  }
  uint16_t a = 0;
  if (!ParseHexAddr16(expr, &a)) return false;
  out->symbolic = false;
  out->addr = a;
  return true;
}

// Parse watchpoint expression using `/` (or `:`) delimiter:
//   `r/0xFFE8`, `w/0x0020/5`, `x/0x1019/3`, `/0x1000`, `any/0x1000`, `@kernel+0x1B`, `0x1000` (when allow_untyped).
inline bool ParseWatchExpr(const char* s, bool allow_untyped, char untyped_default_type,
                           uint32_t default_count, WatchSpec* out) {
  if (!s || !*s) return false;
  *out = WatchSpec{};
  // Split into up to 3 segments on '/' or ':'
  char segs[3][64];
  int nseg = 0;
  const char* p = s;
  // Leading `/0x1000` means untyped "any" watchpoint
  if (*p == '/' || *p == ':') {
    strcpy(segs[nseg++], "any");
    p++;
  }
  while (*p) {
    if (nseg >= 3) return false;
    const char* end = p;
    while (*end && *end != '/' && *end != ':') end++;
    size_t slen = (size_t)(end - p);
    if (slen == 0 || slen >= sizeof(segs[0])) return false;
    memcpy(segs[nseg], p, slen);
    segs[nseg][slen] = 0;
    nseg++;
    if (*end) {
      p = end + 1;
      if (!*p) return false;  // trailing delimiter
    } else {
      break;
    }
  }
  if (nseg == 0) return false;

  int addr_idx = 0;
  char wtype = untyped_default_type;
  if ((strlen(segs[0]) == 1 && (ToLowerAscii(segs[0][0]) == 'r' ||
                                ToLowerAscii(segs[0][0]) == 'w' ||
                                ToLowerAscii(segs[0][0]) == 'x')) ||
      StrEqIgnoreCase(segs[0], "any")) {
    if (nseg < 2) return false;
    wtype = StrEqIgnoreCase(segs[0], "any") ? (char)0 : ToLowerAscii(segs[0][0]);
    addr_idx = 1;
  } else {
    if (segs[0][0] == '@') {
      // `@module[+offset]` defaults to 'x' for trigger/stop, or `untyped_default_type` (0) for logging watch
      wtype = untyped_default_type;
    } else if (!allow_untyped) {
      return false;
    }
    addr_idx = 0;
  }

  if (!ParseAddrTarget(segs[addr_idx], out)) return false;
  out->type = wtype;
  out->count = default_count;

  if (addr_idx + 1 < nseg) {
    if (addr_idx + 2 != nseg) return false;
    uint64_t c = 0;
    if (!ParseCount(segs[addr_idx + 1], &c) || c == 0 || c > 0xFFFFFFFFULL) return false;
    out->count = (uint32_t)c;
  }
  return true;
}

// Resolve `@module[+|-offset]` against primordial OS-9 modules in a 64 KB RAM image.
inline bool ResolveModuleWatchpoint(const uint8_t* ram64k, WatchSpec& wp, char* err_buf, size_t err_cap) {
  if (!wp.symbolic) return true;
  for (uint32_t i = 0; i <= 0xFEF7; i++) {
    if (ram64k[i] != 0x87 || ram64k[i + 1] != 0xCD) continue;
    uint16_t size = ((uint16_t)ram64k[i + 2] << 8) | ram64k[i + 3];
    if (size < 9 || i + size > 65536) continue;
    uint16_t namoff = ((uint16_t)ram64k[i + 4] << 8) | ram64k[i + 5];
    if (namoff >= size) continue;
    uint8_t chk = 0;
    for (int j = 0; j < 9; j++) chk ^= ram64k[i + j];
    if (chk != 0xFF) continue;

    char mname[32];
    size_t nlen = 0;
    bool ok_name = true;
    for (uint32_t p = i + namoff; p < i + size; p++) {
      uint8_t b = ram64k[p];
      uint8_t ch = b & 0x7F;
      if (ch < 32 || ch > 126 || nlen + 1 >= sizeof(mname)) { ok_name = false; break; }
      mname[nlen++] = (char)ch;
      if (b & 0x80) break;
    }
    if (!ok_name || nlen == 0) continue;
    mname[nlen] = 0;

    if (StrEqIgnoreCase(mname, wp.mod_name)) {
      int32_t resolved = (int32_t)i + wp.mod_offset;
      if (resolved < 0 || resolved > 0xFFFF) {
        if (err_buf && err_cap)
          snprintf(err_buf, err_cap, "address for '@%s%+d' out of 64KB range", wp.mod_name, (int)wp.mod_offset);
        return false;
      }
      wp.addr = (uint16_t)resolved;
      wp.symbolic = false;
      return true;
    }
  }
  if (err_buf && err_cap)
    snprintf(err_buf, err_cap, "OS-9 module '@%s' not found in loaded 64KB image", wp.mod_name);
  return false;
}

struct TurbolabPlan {
  bool trace_explicit = false;
  uint8_t trace_flags = 0;
  uint64_t trigger_cycle = 0;
  uint64_t trigger_time_us = 0;
  bool has_trigger_wp = false;
  WatchSpec trigger_wp;

  uint64_t trace_count = 0;     // stop after emitting N trace records (0 = unlimited)
  uint64_t max_cycles = 0;      // stop at CPU cycle #N (0 = unlimited)
  uint64_t max_time_us = 0;     // stop after wall-clock duration (0 = unlimited)
  bool has_stop_wp = false;
  WatchSpec stop_wp;

  int num_watches = 0;
  WatchSpec watches[kMaxWatches];

  char image[80] = {};
};

inline bool ParseTriggerSpec(const char* val, TurbolabPlan& plan) {
  if (!val || !*val) return false;
  // 1. Explicit `c/<count>` or `c:<count>`
  if ((ToLowerAscii(val[0]) == 'c') && (val[1] == '/' || val[1] == ':')) {
    return ParseCount(val + 2, &plan.trigger_cycle);
  }
  // 2. Explicit `s/<dur>` or `t/<dur>`
  if ((ToLowerAscii(val[0]) == 's' || ToLowerAscii(val[0]) == 't') && (val[1] == '/' || val[1] == ':')) {
    return ParseDurationUs(val + 2, &plan.trigger_time_us, /*allow_bare_seconds=*/true);
  }
  // 3. Watchpoint (`r/0xFFE8`, `w/0x0020/5`, `x/0x1019/3`, `/0x1000`, `@kernel+0x1B`)
  WatchSpec wp;
  if (ParseWatchExpr(val, /*allow_untyped=*/false, /*untyped_default_type=*/'x', /*default_count=*/1, &wp)) {
    plan.has_trigger_wp = true;
    plan.trigger_wp = wp;
    return true;
  }
  // 4. Duration with explicit unit (`500ms`, `2s`, `100us`)
  if (ParseDurationUs(val, &plan.trigger_time_us, /*allow_bare_seconds=*/false)) {
    return true;
  }
  // 5. Bare cycle count (`50000`, `50k`, `16K`) -- note `0x...`/`$...` without `r/|w/|x/` is NOT a bare cycle count
  if (val[0] != '$' && !(val[0] == '0' && (val[1] == 'x' || val[1] == 'X'))) {
    return ParseCount(val, &plan.trigger_cycle);
  }
  return false;
}

inline bool ParseStopSpec(const char* val, TurbolabPlan& plan) {
  if (!val || !*val) return false;
  if ((ToLowerAscii(val[0]) == 'c') && (val[1] == '/' || val[1] == ':')) {
    return ParseCount(val + 2, &plan.max_cycles);
  }
  if ((ToLowerAscii(val[0]) == 't' || ToLowerAscii(val[0]) == 's') && (val[1] == '/' || val[1] == ':')) {
    return ParseDurationUs(val + 2, &plan.max_time_us, /*allow_bare_seconds=*/true);
  }
  WatchSpec wp;
  if (ParseWatchExpr(val, /*allow_untyped=*/false, /*untyped_default_type=*/'x', /*default_count=*/1, &wp)) {
    plan.has_stop_wp = true;
    plan.stop_wp = wp;
    return true;
  }
  if (ParseDurationUs(val, &plan.max_time_us, /*allow_bare_seconds=*/false)) {
    return true;
  }
  if (val[0] != '$' && !(val[0] == '0' && (val[1] == 'x' || val[1] == 'X'))) {
    return ParseCount(val, &plan.max_cycles);
  }
  return false;
}

}  // namespace turbolab
