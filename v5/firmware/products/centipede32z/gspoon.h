#ifndef _GSPOON_H_
#define _GSPOON_H_

// gspoon.h -- Gerbil Spoonfeeder for Centipede 32z (v5).
//
// Core 1 (`SpoonfeedConsoleOnReset` / `DriveConsole`, strictly IN_RAM with zero flash calls):
//   Keeps the real CoCo 6809E spinning on `JMP $7E7E` while servicing `BG2FG_PEEK` / `BG2FG_POKE`
//   so Core 0 (`BackgroundSpoonFeeder`) can render the 40x24 PMODE4 Tcl console on the CoCo's VDG
//   and scan the CoCo keyboard alongside USB CDC (`tcl_io::IO_USB | tcl_io::IO_COCO2`).
//   On `BG2FG_EXIT_CONSOLE` (`bye`), configures SAM/VDG (preserving 16 KB SAM M0 `$FFDB` on CoCo 2),
//   spoonfeeds `LDS` (if requested) + `JMP <entry>`, and returns into the selected IN_RAM Gerbil engine!

#ifndef TCL_BYE
#define TCL_BYE 9
#endif

namespace gspoon {

volatile bool drive_console_ready = false;

struct addr_byte {
  uint a;
  byte b;
};

struct addr_byte __not_in_flash("coco2_reset") Coco2StartupGPokes[] = {
    // PIA0
    {0xff01, 0x00},  // choose data directions
    {0xff03, 0x00},  // choose data directions
    {0xff00, 0x00},  // PA* all inputs
    {0xff02, 0xff},  // PB* all outputs
    {0xff01, 0x34},  // CA2 is 0. Choose data. disable irqA (irq on HS)
    {0xff03, 0x34},  // CB2 is 0. Choose data. disable irqB (irq on FS)
    // PIA1
    {0xff21, 0x00},  // choose data directions
    {0xff23, 0x00},  // choose data directions
    {0xff20, 0xfe},  // PA* outputs, except PA0 input.
    {0xff22, 0xfe},  // PB* outputs, except PB0 input.
    {0xff21, 0x34},  // CA2 is 0. Choose data. disable irqA (firq on rs232)
    {0xff23, 0x34},  // Disables IRQB by CB1
    {0xff22, 0x00},  // PB* are zero.
    {0xff20, 0x02},  // PA* are 0, except PA1 is 1 (rs232 out)
    {0, 0}};

static const PIO kPio = pio0;
static constexpr uint kSm = 0;

#define GSPOON_GET()      gerbil_program_get_word(kPio, kSm)
#define GSPOON_DRIVE(b)   gerbil_program_put_word(kPio, kSm, 0x100u | (uint32_t)(byte)(b))
#define GSPOON_PASS()     gerbil_program_put_word(kPio, kSm, 0x000u)

#define GSPOON_PREMISE                                           \
  bool ok = true;                                                \
  const uint signals = GSPOON_GET();                             \
  const bool reading = ((signals & (1u << G_RW)) != 0);          \
  const uint abus = (uint)(sio_hw->gpio_hi_in & 0xFFFFu);        \
  if (want_addr && abus != want_addr) ok = false;                \
  byte dbus = feed_data;

bool IN_RAM IdleStep(uint want_addr = 0, byte feed_data = 0) {
  want_addr = 0xFFFF;
  GSPOON_PREMISE
  if (reading) {
    GSPOON_DRIVE(0xFF);
  } else {
    ok = false;
    dbus = (byte)GSPOON_GET();
  }
  (void)dbus;
  return ok;
}

byte IN_RAM GrabStep() {
  constexpr uint want_addr = 0;
  constexpr byte feed_data = 0;
  GSPOON_PREMISE
  if (reading) {
    GSPOON_PASS();
    dbus = (byte)GSPOON_GET();
  } else {
    ok = false;
    dbus = (byte)GSPOON_GET();
  }
  (void)ok;
  return dbus;
}

bool IN_RAM ReadStep(uint want_addr = 0, byte feed_data = 0) {
  GSPOON_PREMISE
  if (reading) {
    GSPOON_DRIVE(feed_data);
  } else {
    ok = false;
    dbus = (byte)GSPOON_GET();
  }
  (void)dbus;
  return ok;
}

bool IN_RAM WriteStep(uint want_addr = 0, byte feed_data = 0) {
  GSPOON_PREMISE
  if (reading) {
    ok = false;
    GSPOON_DRIVE(0xFF);
  } else {
    dbus = (byte)GSPOON_GET();
  }
  if (feed_data && dbus != feed_data) ok = false;
  return ok;
}

uint IN_RAM AnyStep() {
  constexpr uint want_addr = 0;
  byte feed_data = 0x7E;
  GSPOON_PREMISE
  if (reading) {
    GSPOON_DRIVE(feed_data);
  } else {
    dbus = (byte)GSPOON_GET();
  }
  (void)ok;
  (void)dbus;
  return abus;
}

// Sync on two consecutive executions of the 4-cycle instruction "JMP $7E7E".
void IN_RAM Synchronize7E() {
  uint a;
  while (true) {
    a = AnyStep(); if (a != 0x7e7e) continue;
    a = AnyStep(); if (a != 0x7e7f) continue;
    a = AnyStep(); if (a != 0x7e80) continue;
    a = AnyStep(); if (a != 0xffff) continue;
    a = AnyStep(); if (a != 0x7e7e) continue;
    a = AnyStep(); if (a != 0x7e7f) continue;
    a = AnyStep(); if (a != 0x7e80) continue;
    a = AnyStep(); if (a != 0xffff) continue;
    break;
  }
}

void IN_RAM Jump(uint a) {
  Synchronize7E();
  ReadStep(0, 0x7E);  // JMP extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
}

void IN_RAM MaskInterrupts() {
  Synchronize7E();
  ReadStep(0, 0x1A);  // ORCC #$50 (mask IRQ and FIRQ, matching 6809 hardware RESET)
  ReadStep(0, 0x50);
  IdleStep();
}

void IN_RAM SetS(uint s) {
  MaskInterrupts();

  Synchronize7E();
  ReadStep(0, 0x10);  // LDS #immediate
  ReadStep(0, 0xCE);
  ReadStep(0, (byte)(s >> 8));
  ReadStep(0, (byte)s);
}

byte IN_RAM GPeek1(uint a) {
  Synchronize7E();
  ReadStep(0, 0xF6);  // LDB extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
  IdleStep();
  return GrabStep();
}

void IN_RAM GPoke1(uint a, byte x) {
  Synchronize7E();
  ReadStep(0, 0xCC);  // LDD #immediate
  ReadStep(0, 0);
  ReadStep(0, x);
  ReadStep(0, 0xF7);  // STB extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
  IdleStep();
  WriteStep(a, x);
  // Mirror low-64K writes into RP2350 `ram[]` so `tether --just-get-pmode` can capture the PMODE4 Tcl console too!
  if (a < 0x8000u) {
    ram[a] = x;
  }
}

void IN_RAM draw_large_v() {
  constexpr uint SCREEN_BASE = 0x0800;
  constexpr uint SCREEN_HEIGHT = 192;
  constexpr uint BYTES_PER_ROW = 32;

  for (int y = 0; y < (int)SCREEN_HEIGHT; y++) {
    int x_left = (y * 127) / 191;
    int x_right = 255 - x_left;
    uint16_t addr_left = (uint16_t)(SCREEN_BASE + (y * BYTES_PER_ROW) + (x_left / 8));
    uint8_t mask_left = (uint8_t)(0x80u >> (x_left % 8));
    uint16_t addr_right = (uint16_t)(SCREEN_BASE + (y * BYTES_PER_ROW) + (x_right / 8));
    uint8_t mask_right = (uint8_t)(0x80u >> (x_right % 8));
    GPoke1(addr_left, mask_left);
    GPoke1(addr_right, mask_right);
  }
}

void IN_RAM DriveConsole() {
  // Flush stale fg2bg items while preserving FG2BG_SPOON_ON_RESET.
  {
    uint discard;
    bool saw_spoon = false;
    while (fg2bg.pop(discard)) {
      if ((discard >> 24) == FG2BG_SPOON_ON_RESET) saw_spoon = true;
    }
    if (saw_spoon) {
      spoon_has_work = true;
    }
  }

  drive_console_ready = true;

  // Re-initialize PIA0 for keyboard scanning.
  GPoke1(0xFF01, 0x00);
  GPoke1(0xFF00, 0x00);
  GPoke1(0xFF01, 0x34);
  GPoke1(0xFF03, 0x00);
  GPoke1(0xFF02, 0xFF);
  GPoke1(0xFF03, 0x34);
  g_pia0_ff02 = 0xFF;

  tcl_io::active_io |= tcl_io::IO_COCO2;

  while (true) {
    uint z = 0;
    AnyStep();

    if ((sio_hw->gpio_in & (1u << G_RESET)) == 0) {
      (void)fg2bg.push((uint)FG2BG_HOST_RESET << 24);
      while (true) {
        tight_loop_contents();
      }
    }

    if (bg2fg.pop(z)) {
      uint cmd = z >> 24;
      uint addr = (z >> 8) & 0xFFFFu;
      byte data = (byte)(z & 0xFFu);

      if (cmd == BG2FG_PEEK) {
        byte val = GPeek1(addr);
        (void)fg2bg.push(((uint)FG2BG_PEEK_REPLY << 24) | (addr << 8) | val);
      } else if (cmd == BG2FG_POKE) {
        GPoke1(addr, data);
      } else if (cmd == BG2FG_EXIT_CONSOLE) {
        drive_console_ready = false;
        tcl_io::active_io &= ~tcl_io::IO_COCO2;

        // Reset SAM VDG mode bits (V0..V2) and display offset bits (F0..F6) to 32x16 text at $0400.
        for (uint a = 0xFFC0; a <= 0xFFD2; a += 2) {
          GPoke1(a, 0);
        }
        GPoke1(0xFFC9, 0);  // F1=1: framebuffer at $0400
#if SAM_BIT_16K
        GPoke1(0xFFDB, 0);  // M0=1: preserve 16 KB DRAM mode on CoCo 2 SAM!
#endif
#if SAM_BIT_64K
        GPoke1(0xFFDD, 0);  // M1=1: preserve 64 KB DRAM mode on SAM
#endif
        // Reset VDG on PIA1 to 32x16 green alphanumeric text mode.
        byte low_bits = GPeek1(0xFF22) & 0x07;
        GPoke1(0xFF22, low_bits);

        if (g_launch_mode == LAUNCH_PRELOAD_DECB || g_launch_mode == LAUNCH_OS_DIRECT) {
          // Copy prepared 32x16 VDG text screen ($0400..$05FF) into CoCo 2 motherboard DRAM so monitor shows status!
          for (uint a = 0x0400; a < 0x0600; a++) {
            GPoke1(a, ram[a]);
          }
          GPoke1(0xFFD4, 0);  // SAM P1=0
          GPoke1(0xFFDF, 0);  // SAM TY=1 (all-RAM mode: silence CoCo 2 internal BASIC ROMs!)
          SamP1Bit = false;
          SamTyBit = true;
          MaskInterrupts();
          if (g_launch_initial_sp != 0) {
            SetS(g_launch_initial_sp);
          }
          if (g_coco_irq_shim_active) {
            (void)GPeek1(0xFF02);   // clear any pending PIA0 VSYNC flag
            GPoke1(0xFF03, 0x35);   // enable CoCo 2 PIA0 60 Hz VSYNC _IRQ
          }
          Jump(g_launch_exec_addr);
          return;
        }

        if (g_launch_mode == LAUNCH_HOST_COCO3) {
          // Real CoCo 3 (`on coco3 ...` / `gerbil_io`): clear warm-start flags and jump through host ($FFFE).
          GPoke1(0x0071, 0);
          GPoke1(0x0072, 0);
          GPoke1(0x0073, 0);
          GPoke1(0xFEED, 0);
          uint16_t vec = ((uint16_t)GPeek1(0xFFFE) << 8) | GPeek1(0xFFFF);
          Jump(vec);
          return;
        }

        // Standard CoCo 2 or Become-CoCo-3 BASIC cold boot:
        GPoke1(0xFFD4, 0);  // SAM P1=0
        GPoke1(0xFFDE, 0);  // SAM TY=0 (ROM mode)
        SamP1Bit = false;
        SamTyBit = false;

        // Clear warm-start flags in both RAM banks and physical motherboard DRAM.
        ram[0x10071] = 0;
        ram[0x10072] = 0;
        ram[0x10073] = 0;
        ram[0x1FEED] = 0;
        ram[0x10088] = 0x04;
        ram[0x10089] = 0x00;
        ram[0x0071] = 0;
        ram[0x0072] = 0;
        ram[0x0073] = 0;
        ram[0xFEED] = 0;
        GPoke1(0x0071, 0);
        GPoke1(0x0072, 0);
        GPoke1(0x0073, 0);
        GPoke1(0xFEED, 0);

        if (g_launch_mode == LAUNCH_RSBASIC_COCO3) {
          g_gime_init0 = 0x0A;
          Jump(0xC000);
        } else {
          Jump(0xA027);
        }
        return;
      }
    }
  }
}

void IN_RAM SpoonfeedConsoleOnReset() {
  coco_running = false;

  for (auto* p = Coco2StartupGPokes; p->a; p++) {
    GPoke1(p->a, p->b);
  }

  // PMODE4 white-on-black (or green-on-black if GREEN_PMODE):
  byte low_bits = GPeek1(0xFF22) & 0x07;
#if GREEN_PMODE
  GPoke1(0xFF22, 0xF0 | low_bits);
#else
  GPoke1(0xFF22, 0xF8 | low_bits);
#endif

  // Clear all SAM bits, set 16K/64K DRAM size, set V=6 (PMODE4) and F=2 ($0800).
  for (uint a = 0xFFC0; a < 0xFFE0; a += 2) {
    GPoke1(a, 42);
  }
#if SAM_BIT_16K
  GPoke1(0xFFDB, 42);  // M0=1 for 16 KB CoCo 2 DRAM!
#endif
#if SAM_BIT_64K
  GPoke1(0xFFDD, 42);  // M1=1 for 64 KB DRAM
#endif
  GPoke1(0xFFC0, 42);
  GPoke1(0xFFC3, 42);
  GPoke1(0xFFC5, 42);
  GPoke1(0xFFCB, 42);  // Framebuffer at $0800

  for (uint a = 0x0800; a < 0x2000; a++) {
    GPoke1(a, 0);
  }

  // Zero shadow_fb in SRAM without calling flash `memset`!
  uint32_t* sfb = reinterpret_cast<uint32_t*>(console::shadow_fb);
  for (size_t i = 0; i < sizeof(console::shadow_fb) / sizeof(uint32_t); i++) {
    sfb[i] = 0;
  }
  console::cursor_row = 0;
  console::cursor_col = 0;

  draw_large_v();

  // Clear warm-start flags in CoCo RAM.
  GPoke1(0x0071, 0x00);
  GPoke1(0x0072, 0x00);
  GPoke1(0x0073, 0x00);
  GPoke1(0xFEED, 0x00);

  if (!spoon_has_work) {
    (void)fg2bg.push((uint)FG2BG_SPOON_ON_RESET << 24);
  }

  DriveConsole();
}

void SleepMillis(Coro* c, uint64_t ms) {
  uint32_t start_sec, start_ms;
  get_system_time(&start_sec, &start_ms);
  uint64_t start_total = (uint64_t)start_sec * 1000ULL + start_ms;

  coro_yield(c);
  while (true) {
    uint32_t cur_sec, cur_ms;
    get_system_time(&cur_sec, &cur_ms);
    uint64_t cur_total = (uint64_t)cur_sec * 1000ULL + cur_ms;
    if (cur_total - start_total >= ms) break;
    coro_yield(c);
  }
}

void BackgroundSpoonFeeder(Coro* coro_self) {
  console::inkey_state iks = {};
  g_spoon_coro = coro_self;
  rpc::g_vfs_coro = coro_self;
  g_centipede_launch_prepared = false;

  if (::startup_e_clock_detected) {
    draw_bug_pmode4_splash_screen();
    SleepMillis(coro_self, BUG_SPLASH_MILLIS);
  }

  tcl_io::emit_string("COPICO CENTIPEDE 32Z CONSOLE (V5)\n");

  bool booted_via_rpc = false;
  if (::boot_mode_check == ::boot_mode + BOOT_MODE_CHECKER) {
    ::boot_mode_check = 0;
    booted_via_rpc = true;
  } else {
    if (!::startup_e_clock_detected) {
      ::boot_mode = 2;  // USB-only pseudo-default (stays in REPL)
    } else {
      ::boot_mode = 1;  // CoCo powered on default
      bool found = false;
      for (int col = 0; col < 8 && !found; col++) {
        console::poke(0xFF02, (unsigned char)~(1 << col));
        unsigned char row_bits = ~console::peek(0xFF00) & 0x7F;
        for (int row = 0; row < 7 && !found; row++) {
          if (row_bits & (1 << row)) {
            unsigned char k = console::unshifted_map[col][row];
            if (k == 27 || k == 12 || (k >= '0' && k <= '9') || k == '@') {
              ::boot_mode = k;
              found = true;
            } else if (k >= 'a' && k <= 'z') {
              ::boot_mode = k - 'a' + 'A';
              found = true;
            } else if (k >= 'A' && k <= 'Z') {
              ::boot_mode = k;
              found = true;
            }
          }
        }
      }
    }
  }
  cobs_printf("boot_mode=%u (e_clock=%d)\n", (unsigned)boot_mode, ::startup_e_clock_detected ? 1 : 0);

  if (booted_via_rpc) {
    cobs_printf("booted_via_rpc: waiting for tether USB...\n");
    uint32_t waited_ms = 0;
    while (!usb_tether_ok() && waited_ms < 5000) {
      SleepMillis(coro_self, 50);
      waited_ms += 50;
    }
    SleepMillis(coro_self, 250);
    rtc_poll_walltime();
  }

  if (boot_mode == 70 || boot_mode == 1170) {
    g_config.fuzix = true;
    g_config.trace_writes = (boot_mode == 1170);
    goto BYE;
  }

  if (boot_mode != 27) {
    struct vfs_info st;
    if (vfs_stat("/rc/init.tcl", &st) == 0) {
      int init_result = Tcl_Eval(global_tcl_interp, const_cast<char*>("source /rc/init.tcl"), 0, (char**)0);
      if (init_result == TCL_ERROR) {
        const char* output = global_tcl_interp->result;
        if (output && output[0]) {
          tcl_io::emit('?');
          tcl_io::emit_string(output);
          tcl_io::emit('\n');
        }
      } else if (init_result == TCL_BYE) {
        tcl_io::emit_string("[BYE]\n");
        goto BYE;
      }
    }

    char path_buf[40];
    snprintf(path_buf, sizeof(path_buf), "/rc/mode%u.tcl", (unsigned)boot_mode);
    if (vfs_stat(path_buf, &st) != 0 && usb_tether_ok()) {
      snprintf(path_buf, sizeof(path_buf), "/pc/mode%u.tcl", (unsigned)boot_mode);
    }
    if (vfs_stat(path_buf, &st) == 0) {
      char cmd_buf[56];
      snprintf(cmd_buf, sizeof(cmd_buf), "source %s", path_buf);
      int mode_result = Tcl_Eval(global_tcl_interp, cmd_buf, 0, (char**)0);
      if (mode_result == TCL_ERROR) {
        const char* output = global_tcl_interp->result;
        if (output && output[0]) {
          tcl_io::emit('?');
          tcl_io::emit_string(output);
          tcl_io::emit('\n');
        }
      } else if (mode_result == TCL_BYE) {
        tcl_io::emit_string("[BYE]\n");
        goto BYE;
      }
    }
  }

  {
    bool coco2_welcomed = false;
    static std::string history[20];
    static int history_count = 0;

    while (true) {
      while (tcl_io::active_io == 0) {
        SleepMillis(coro_self, 50);
      }

      char line[256];
      int line_len = 0;
      int line_cursor = 0;
      int history_index = history_count;
      std::string current_edit = "";

      auto redraw_line = [&]() {
        tcl_io::emit('\r');
        tcl_io::emit_string("    TCL>");
        for (int i = 0; i <= line_len; i++) {
          if (i == line_cursor) tcl_io::emit_string("\x1b[7m");
          if (i < line_len) tcl_io::emit(line[i]);
          else              tcl_io::emit(' ');
          if (i == line_cursor) tcl_io::emit_string("\x1b[0m");
        }
        tcl_io::emit_string("\x1b[K");
        for (int i = 0; i < (line_len - line_cursor + 1); i++) tcl_io::emit(8);
      };

      redraw_line();

      while (true) {
        if (!g_pending_injections.empty()) {
          pcb::RpcRequest req = g_pending_injections.front();
          g_pending_injections.erase(g_pending_injections.begin());

          std::string cmd = req.data;
          cobs_printf("\n[Injecting: %s]\n", cmd.c_str());

          int rc = Tcl_Eval(global_tcl_interp, (char*)cmd.c_str(), 0, (char**)0);
          if (rc == TCL_BYE) {
            // Prepare launch (including any `/pc/...` VFS reads) BEFORE replying so `tether -just-inject` is still serving `/pc`!
            centipede_prepare_launch_on_core0();
          }

          pcb::RpcResponse resp;
          resp.serial = req.serial;
          resp.status = rc;
          if (global_tcl_interp->result && global_tcl_interp->result[0]) {
            resp.message = global_tcl_interp->result;
            resp.data = global_tcl_interp->result;
          }
          pico_rpc::send_response(resp);

          if (rc == TCL_BYE) goto BYE;
          redraw_line();
          continue;
        }

        if ((tcl_io::active_io & tcl_io::IO_COCO2) && !coco2_welcomed) {
          console::emit_char_string("COPICO CENTIPEDE 32Z CONSOLE (V5)\n");
          coco2_welcomed = true;
          redraw_line();

          for (int col = 0; col < 8; col++) {
            coro_yield(coro_self);
            console::poke(0xFF02, (unsigned char)~(1 << col));
            coro_yield(coro_self);
            unsigned char row_bits = ~console::peek(0xFF00) & 0x7F;
            coro_yield(coro_self);
            iks.prev_pressed[col] = row_bits;
            iks.debounced_pressed[col] = row_bits;
            coro_yield(coro_self);
          }
          coro_yield(coro_self);
          console::poke(0xFF02, 0xFF);
          coro_yield(coro_self);
        }

        byte key = tcl_io::poll_key(&iks);
        if (key == 0) {
          uint64_t start_ticks = get_system_ticks_20ms();
          while (get_system_ticks_20ms() == start_ticks) {
            coro_yield(coro_self);
          }
          continue;
        }

        if (key == 128) {  // Up
          if (history_index > 0) {
            if (history_index == history_count) {
              line[line_len] = '\0';
              current_edit = line;
            }
            history_index--;
            strcpy(line, history[history_index].c_str());
            line_len = (int)strlen(line);
            line_cursor = line_len;
            redraw_line();
          }
          continue;
        }
        if (key == 129) {  // Down
          if (history_index < history_count) {
            history_index++;
            if (history_index == history_count) strcpy(line, current_edit.c_str());
            else                                strcpy(line, history[history_index].c_str());
            line_len = (int)strlen(line);
            line_cursor = line_len;
            redraw_line();
          }
          continue;
        }
        if (key == 130) {  // Left
          if (line_cursor > 0) { line_cursor--; redraw_line(); }
          continue;
        }
        if (key == 131) {  // Right
          if (line_cursor < line_len) { line_cursor++; redraw_line(); }
          continue;
        }
        if (key == 13 || key == 10) {  // Enter
          tcl_io::emit('\r');
          tcl_io::emit_string("    TCL>");
          line[line_len] = '\0';
          tcl_io::emit_string(line);
          tcl_io::emit_string("\x1b[K\n");
          break;
        }
        if (key == 8 || key == 127) {  // Backspace / DEL
          if (line_cursor > 0) {
            for (int i = line_cursor - 1; i < line_len - 1; i++) line[i] = line[i + 1];
            line_len--;
            line_cursor--;
            redraw_line();
          }
          continue;
        }
        if (key >= 0x20 && key < 128 && line_len < 254) {
          for (int i = line_len; i > line_cursor; i--) line[i] = line[i - 1];
          line[line_cursor] = (char)key;
          line_len++;
          line_cursor++;
          redraw_line();
          continue;
        }
      }
      line[line_len] = '\0';

      if (line_len > 0) {
        if (history_count == 0 || history[history_count - 1] != line) {
          if (history_count < 20) {
            history[history_count++] = line;
          } else {
            for (int i = 0; i < 19; i++) history[i] = history[i + 1];
            history[19] = line;
          }
        }

        int result;
#if AUTO_GLOB
        if (strchr(line, ';') == NULL && strchr(line, '[') == NULL &&
            strchr(line, ']') == NULL && strchr(line, '{') == NULL &&
            strchr(line, '}') == NULL && strchr(line, '"') == NULL) {
          char new_line[260];
          snprintf(new_line, sizeof(new_line), "fs %s", line);
          result = Tcl_Eval(global_tcl_interp, new_line, 0, (char**)0);
        } else {
          result = Tcl_Eval(global_tcl_interp, line, 0, (char**)0);
        }
#else
        result = Tcl_Eval(global_tcl_interp, line, 0, (char**)0);
#endif
        const char* output = global_tcl_interp->result;
        if (output && output[0]) {
          if (result == TCL_ERROR) tcl_io::emit('?');
          tcl_io::emit_string(output);
          tcl_io::emit('\n');
        }
        if (result == TCL_BYE) {
          tcl_io::emit_string("[BYE]\n");
          goto BYE;
        }
      }
    }
  }

BYE:
  // Configure target devices, ROMs/OS images, IODriveMask, and g_gerbil_loop_fn on Core 0 BEFORE unhalting Core 1!
  centipede_prepare_launch_on_core0();

  uint cmd = ((uint)BG2FG_EXIT_CONSOLE << 24);
  while (!bg2fg.push(cmd)) {
    SleepMillis(coro_self, 1);
  }
  cobs_printf("BackgroundSpoonFeeder: bye, returning to background.\n");
}

}  // namespace gspoon

#endif  // _GSPOON_H_
