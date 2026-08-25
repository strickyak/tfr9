#include "cobs_tx.h"
#include "tcl_io.h"
#include "vfs.h"
#include "coro.h"
#include "rtc.h"
#include "pcb.h"

extern void rp2350_reset_standard(void);

namespace pico_rpc {
  void send_response(const pcb::RpcResponse& resp);
}
extern std::vector<pcb::RpcRequest> g_pending_injections;

#ifndef _GSPOON_H_
#define _GSPOON_H_

#define TCL_BYE 9

// Gerbil SPOON

// The Gerbil continues to operate the PIO wheel,
// so this is called "gspoon".

namespace gspoon {

volatile bool drive_console_ready = false;

struct addr_byte {
  uint a;
  byte b;
};

struct addr_byte Coco2StartupGPokes[] = {
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
    {0xff20, 0xfe},  // PA* outputs, execpt PA0 input.
    {0xff22, 0xfe},  // PB* outputs, execpt PB0 input. // add
    //
    {0xff21, 0x34},  // CA2 is 0. Choose data. disable irqA (firq on rs232)
    // {0xff23, 0x37},  // Enables IRQB by CB1
    {0xff23, 0x34},  // Disables IRQB by CB1
    {0xff22, 0x00},  // PB* are zero.
    {0xff20, 0x02},  // PA* are 0. execpt PA1 is 1 (rs232 out)
    // END
    {0, 0}};

static constexpr uint MAX_LOGS = 100;

struct LogItem {
  char kind;
  uint abus;
  uint want_abus;
  byte dbus;
  byte want_dbus;
  const char* mark;
};
LogItem Logs[MAX_LOGS];
uint Log_step;

const PIO pio = pio0;
constexpr uint sm = 0;

void PrintLog() {
  cobs_printf("----\n");
  uint j = 1;
  for (uint i = 0; i < Log_step; i++) {
    struct LogItem* p = Logs + i;
    if (p->mark) {
      cobs_printf("----- %s\n", p->mark);
      j = 1;
    } else {
      char k = p->kind;
      if (k < 32 || k > 126) k = '?';
      cobs_printf("[%3d.] '%c' %04x %02x (%04x %02x)\n", j, p->kind, p->abus,
                  p->dbus, p->want_abus, p->want_dbus);
      j++;
    }
  }
}

void IN_RAM Mark(const char* mark) {
  struct LogItem* p = Logs + Log_step;
  p->mark = mark;
  ++Log_step;
}

void IN_RAM Log(char kind, uint abus, byte dbus, uint want_addr,
                byte feed_data) {
  if (Log_step < MAX_LOGS) {
    struct LogItem* p = Logs + Log_step;
    p->kind = kind;
    p->abus = abus;
    p->dbus = dbus;
    p->want_abus = want_addr;
    p->want_dbus = feed_data;
    p->mark = nullptr;
    ++Log_step;
  }
}

#define PREMISE                                                  \
  bool ok = true;                                                \
  const uint signals = GERBIL_GET();                             \
  const bool reading = ((signals & (1u << G_RW)) != 0);          \
  const uint abus = (uint)volatile_sio_hw->gpio_hi_in & 0xFFFFu; \
  if (want_addr && abus != want_addr) ok = false;                \
  byte dbus = feed_data;

bool IN_RAM IdleStep(uint want_addr = 0, byte feed_data = 0) {
  want_addr = 0xFFFF;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(0xFF);
  } else {  // ------ case WRITE CYCLE
    ok = false;
    dbus = (byte)GERBIL_GET();
  }
  Log('i' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

byte IN_RAM GrabStep() {
  constexpr uint want_addr = 0;
  constexpr byte feed_data = 0;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_PASS();
    dbus = (byte)(GERBIL_GET());  // log & debug
  } else {                        // ------ case WRITE CYCLE
    ok = false;
    dbus = (byte)GERBIL_GET();
  }
  Log('g' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return dbus;
}

bool IN_RAM ReadStep(uint want_addr = 0, byte feed_data = 0) {
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(feed_data);
  } else {  // ------ case WRITE CYCLE
    ok = false;
    dbus = (byte)GERBIL_GET();
  }
  Log('r' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

bool IN_RAM WriteStep(uint want_addr = 0, byte feed_data = 0) {
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    ok = false;
    GERBIL_DRIVE(0xFF);
  } else {  // ------ case WRITE CYCLE
    dbus = (byte)GERBIL_GET();
  }
  if (feed_data && dbus != feed_data) ok = false;
  Log('w' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

uint IN_RAM AnyStep(bool to_log = true) {
  constexpr uint want_addr = 0;
  byte feed_data = 0x7E;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(feed_data);
  } else {  // ------ case WRITE CYCLE
    dbus = (byte)GERBIL_GET();
  }
  if (to_log) {
    Log('a' - (reading ? 0 : 32), abus, dbus, 0, 0x7E);
  }
  return abus;
}

// Sync on two cycles of the 4-cycle instruction "JMP $7E7E".
void IN_RAM Synchronize7E() {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  uint a;
  while (1) {
    a = AnyStep(false);
    if (a != 0x7e7e) continue;
    a = AnyStep(false);
    if (a != 0x7e7f) continue;
    a = AnyStep(false);
    if (a != 0x7e80) continue;
    a = AnyStep(false);
    if (a != 0xffff) continue;
    a = AnyStep(false);
    if (a != 0x7e7e) continue;
    a = AnyStep(false);
    if (a != 0x7e7f) continue;
    a = AnyStep(false);
    if (a != 0x7e80) continue;
    a = AnyStep(false);
    if (a != 0xffff) continue;
    break;
  }
}

void IN_RAM Jump(uint a) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0x7E);  // 7E => JMP extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
}

byte IN_RAM GPeek1(uint a) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0xF6);  // F6 => LDB extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
  IdleStep();
  return GrabStep();
}

void IN_RAM GPoke1(uint a, byte x) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0xCC);  // CC => LDD #immediate
  ReadStep(0, 0);
  ReadStep(0, (byte)x);

  ReadStep(0, 0xF7);  // F7 => STB extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);

  IdleStep();
  WriteStep(a, x);
}

void IN_RAM GPoke2(uint a, uint x) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0xCC);  // CC => LDD #immediate
  ReadStep(0, (byte)(x >> 8));
  ReadStep(0, (byte)x);

  ReadStep(0, 0xFD);  // FD => STD extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);

  IdleStep();
  WriteStep(a, (byte)(x >> 8));
  WriteStep(a, (byte)x);
}

void IN_RAM DriveConsole() {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.

  // Flush stale fg2bg items (FG2BG_WRITE, etc.) from the bus cycle loop.
  // If fg2bg is full when we try to push PEEK_REPLY, the push silently fails
  // and peek() on the background core hangs, making the keyboard appear dead.
  // CAUTION: Preserve FG2BG_SPOON_ON_RESET — it may have just been pushed
  // and hasn't been consumed by drain_task yet.
  {
    uint discard;
    bool saw_spoon = false;
    while (fg2bg.pop(discard)) {
      if ((discard >> 24) == FG2BG_SPOON_ON_RESET) saw_spoon = true;
    }
    if (saw_spoon) {
      // drain_task hasn't seen this yet — set spoon_has_work directly.
      spoon_has_work = true;
    }
  }

  cobs_printf("DriveConsole: ENTERED\n");
  drive_console_ready = true;

  // Re-initialize PIA0 for keyboard scanning.
  // Coco2StartupGPokes sets CRA=0x34 (bit 2=1, port data selected), but CRA
  // reverts to 0x00 by the time we reach DriveConsole — probably corrupted
  // during the ~6000 GPoke1 screen-clear calls. CRB survives, CRA does not.
  // Re-init PIA0 here to ensure 0xFF00 reads port data (not DDR).
  GPoke1(0xFF01, 0x00);  // CRA: select DDR A
  GPoke1(0xFF00, 0x00);  // DDR A: all inputs (keyboard rows)
  GPoke1(0xFF01, 0x34);  // CRA: select port data, CA2 ctrl
  GPoke1(0xFF03, 0x00);  // CRB: select DDR B
  GPoke1(0xFF02, 0xFF);  // DDR B: all outputs (column drivers)
  GPoke1(0xFF03, 0x34);  // CRB: select port data, CB2 ctrl

  // PIA0 is now ready for keyboard scanning.
  tcl_io::add_coco2();  // BackgroundSpoonFeeder can now use Coco2 I/O

  while (true) {
    uint z = 0;
    AnyStep();  // keep gerbil fed (6809 runs JMP $7E7E)

    { // Check for Coco is being RESET.
        const uint signals = volatile_sio_hw->gpio_in;
        if ((signals & (1 << G_RESET)) == 0) {
            ::rp2350_reset_standard();
        }
    }

    bool ok = bg2fg.pop(z);
    if (ok) {
      uint cmd = z >> 24;
      uint addr = (z >> 8) & 0xFFFF;
      byte data = z & 0xFF;

      if (cmd == BG2FG_PEEK) {
        byte val = GPeek1(addr);
        bool pushed = fg2bg.push(((uint)FG2BG_PEEK_REPLY << 24) |
                                 ((uint)addr << 8) | val);
        if (!pushed) cobs_printf("DC:PEEK_REPLY DROPPED!\n");
      } else if (cmd == BG2FG_POKE) {
        GPoke1(addr, data);
      } else if (cmd == BG2FG_EXIT_CONSOLE) {
        // "bye" from BackgroundSpoonFeeder — reset 6809 and return to normal.
        Jump(0xA027);  // Jump via the 6809 RESET vector
        drive_console_ready = false;
        tcl_io::remove_coco2();  // BackgroundSpoonFeeder continues on USB only
        cobs_printf("DriveConsole: bye, returning to normal foreground.\n");
        return;  // Return to SpoonfeedConsoleOnReset, then to foreground()
      } else {
        cobs_printf("DriveConsole: unknown bg2fg cmd %d\n", cmd);
      }
    }
  }
}

void OldSpoonfeedingExperiments();
void draw_large_v(void);

void IN_RAM SpoonfeedConsoleOnReset() {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.

  // Start BackgroundSpoonFeeder AFTER Coco2StartupGPokes and screen setup,
  // so the PIAs are fully initialized before keyboard scanning begins.
  // (Moved to just before DriveConsole() call below.)

  for (struct addr_byte* p = Coco2StartupGPokes; p->a; p++) {
    GPoke1(p->a, p->b);
  }

#if USE_PMODE4
  // PMODE4: PIA1 PB sets VDG control lines:
  //   PB4 = ~A/G = 1 (graphics mode)
  //   PB3 = CSS  = 1 (alternate color set: white on black)
  // GPoke1(0xFF22, 0x18);
  // GPoke1(0xFF22, 0xF8);  // F8 or F0
  byte low_bits = GPeek1(0xFF22) & 0x07;
#if GREEN_PMODE
  GPoke1(0xFF22, 0xF0 | low_bits);  // used by basic  F3 = green/black
#else
  GPoke1(0xFF22, 0xF8 | low_bits);  // used by basic: FD = white/black
#endif

  // Clear all SAM bits
  for (uint a = 0xFFC0; a < 0xFFE0; a += 2) {
    GPoke1(a, 42);
  }
  // THIS FIXED THE PMODE4 SCREEN: 0xFFDB
  GPoke1(0xFFDB, 42);  // set M0 for 16k

  // Set SAM V0, V1, V2 for PMODE4 (R6G): V=6
  GPoke1(0xFFC0, 42);
  GPoke1(0xFFC3, 42);
  GPoke1(0xFFC5, 42);

  GPoke1(0xFFCB, 42);  // 0x0800

  // Clear 0x0800 to 0x1FFF on the CoCo
  for (uint a = 0x0800; a < 0x2000; a++) {
#if INVERSE_PMODE
    GPoke1(a, 0xFF);
#else
    GPoke1(a, 0);
#endif
  }

  // Reset shadow buffer on the RP2350
#if INVERSE_PMODE
  memset(console::shadow_fb, 0xFF, sizeof(console::shadow_fb));
#else
  memset(console::shadow_fb, 0, sizeof(console::shadow_fb));
#endif
  console::cursor_row = 0;
  console::cursor_col = 0;

  draw_large_v();

#else // not USE_PMODE4
  // Clear all SAM bits except FFC9, so 0x0400 is text frame buffer.
  for (uint a = 0xFFC0; a < 0xFFE0; a += 2) {
    GPoke1(a, 42);
  }
  GPoke1(0xFFC9, 42);  // Use 0x0400 for frame buffer

  for (uint a = 0x0000; a < 0x0600; a++) {
    // Show what page number is being displayed.
    // Expect '4' (top half) and '5' (bottom half).
    GPoke1(a, (byte)('0' + (a >> 8)));
    // was // GPoke1(a, (byte)0xE1);
  }

  // Display that for around 1 seconds, before we continue.
  for (uint k = 0; k < (1 << 20); k++) {
    AnyStep();
  }
  // now change those to little boxes
  for (uint a = 0x0000; a < 0x0600; a++) {
    GPoke1(a, (byte)0xE1);
  }
#endif // USE_PMODE4
  // Then continue with the Console driver.

  // Now start BackgroundSpoonFeeder — PIAs and screen are fully initialized.
  // RACE CONDITION: This pushes FG2BG_SPOON_ON_RESET onto fg2bg, but
  // DriveConsole() (called next) flushes fg2bg to clear stale cycle entries.
  // If drain_task hasn't consumed this message yet, DriveConsole's flush
  // would discard it — so DriveConsole explicitly checks for it during
  // the flush and sets spoon_has_work directly if found.
  if (!spoon_has_work) {
    PUSH_TO_BG(FG2BG_SPOON_ON_RESET, 0, 0);
  }

  DriveConsole();
}  // end SpoonfeedConsoleOnReset

#if GSPOON_POC_DEMO
// This succeeded for a spoon-feeding Proof-Of-Concept that sends NMI
// two seconds after boot to a coco2, capturing the registers
// during the write cycles, keeping control of the coco2 via HALT,
// and then poking "67" on the screen at $0502 by spoon-fed instructions.

void IN_RAM SpoonNMI() {
  bool ok = true;
  ASSERT_NMI();
  Mark("Assert Halt");
  ASSERT_HALT();
  for (uint i = 0; i < 20; i++) AnyStep();
  ASSERT_NMI();
  Mark("Assert NMI");
  for (uint i = 0; i < 2; i++) AnyStep();
  RELEASE_NMI();
  Mark("Release NMI");
  RELEASE_HALT();

  Mark("Release Halt");
  Log_step = 0;

  for (uint i = 0; i < 29; i++) AnyStep();

  Mark("did NMI plus more");

  ASSERT_HALT();
  Mark("AssertHalt, 6");
  for (uint i = 0; i < 6; i++) AnyStep();

  RELEASE_HALT();
  Mark("ReleaseHalt, 2");
  for (uint i = 0; i < 2; i++) AnyStep();

  Mark("LDD #$3637");
  (ReadStep(0, 0xCC));  // CC => LDD #immediate
  (ReadStep(0, 0x36));
  (ReadStep(0, 0x37));

  Mark("STD #$0502");
  (ReadStep(0, 0xFD));  // FD => STD extended
  (ReadStep(0, 0x05));
  (ReadStep(0, 0x02));

  ASSERT_HALT();
  Mark("Assert Halt & RUNOUT 20");
  for (uint i = 0; i < 20; i++) AnyStep();
  SAY('$');
  PrintLog();
  while (1) {
    sleep_us(1);
  }
}
#endif

///////////////////////////////////

void draw_large_v(void) {
  constexpr uint SCREEN_BASE = 0x0800;
  constexpr uint SCREEN_WIDTH = 256;
  constexpr uint SCREEN_HEIGHT = 192;
  constexpr uint BYTES_PER_ROW = 32;   // 256 pixels / 8 bits
  constexpr uint SCREEN_BYTES = 6144;  // 32 bytes * 192 rows

  // Iterate through every scanline (y-axis) from top (0) to bottom (191)
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    // Calculate X coordinates for the left and right lines.
    // As Y goes 0 -> 191, x_left goes 0 -> 127.
    int x_left = (y * 127) / 191;

    // The right line is perfectly symmetrical to the left line.
    int x_right = 255 - x_left;

    // --- Calculate Left Pixel ---
    // Find the specific byte address and bit mask for the left side
    uint16_t addr_left = SCREEN_BASE + (y * BYTES_PER_ROW) + (x_left / 8);
    uint8_t mask_left = 0x80 >> (x_left % 8);  // 0x80 is MSB (leftmost bit)

    // --- Calculate Right Pixel ---
    // Find the specific byte address and bit mask for the right side
    uint16_t addr_right = SCREEN_BASE + (y * BYTES_PER_ROW) + (x_right / 8);
    uint8_t mask_right = 0x80 >> (x_right % 8);

    // GPoke the bytes onto the screen
#if INVERSE_PMODE
    GPoke1(addr_left, 0xFF ^ mask_left);
    GPoke1(addr_right, 0xFF ^ mask_right);
#else
    GPoke1(addr_left, mask_left);
    GPoke1(addr_right, mask_right);
#endif
  }
}

///////////////////////////////////
//
void SleepMillis(Coro* c, uint64_t ms) {
  uint32_t start_sec, start_ms;
  get_system_time(&start_sec, &start_ms);
  double start_time = (double)start_sec + ((double)start_ms / 1000.0);
  double seconds = (double)ms / 1000.0;
  
  coro_yield(c);
  while (true) {
    uint32_t current_sec, current_ms;
    get_system_time(&current_sec, &current_ms);
    double current_time = (double)current_sec + ((double)current_ms / 1000.0);
    if (current_time - start_time >= seconds) {
      break;
    }
    coro_yield(c);
  }
}

Coro* g_spoon_coro = nullptr;

// BackgroundSpoonFeeder runs in the background thread,
// whereas all the above (which should have IN_RAM) run
// in the foreground thread.

void BackgroundSpoonFeeder(Coro* coro_self) {
  console::inkey_state iks = {};
  g_spoon_coro = coro_self;
  rpc::g_vfs_coro = coro_self;  // Let all VFS RPC calls yield
  // Don't block waiting for DriveConsole — start immediately on USB.
  // Coco2 I/O is added dynamically when DriveConsole becomes ready
  // (tcl_io::active_io is updated by the foreground).
  draw_bug_pmode4_splash_screen();

  SleepMillis(coro_self, BUG_SPLASH_MILLIS);

  // Print startup banner
  tcl_io::emit_string("COPICO CENTIPEDE CONSOLE\n");

  bool booted_via_rpc = false;
  if (::boot_mode_check == ::boot_mode + BOOT_MODE_CHECKER) {
      ::boot_mode_check = 0;
      booted_via_rpc = true;
  } else {
      if (!::startup_e_clock_detected) {
        ::boot_mode = 2; // PseudoDefault
      } else {
        ::boot_mode = 1; // Default
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
                ::boot_mode = k - 'a' + 'A';
                found = true;
              }
            }
          }
        }
      }

  }
  cobs_printf("boot_mode=%d\n", boot_mode);

  if (booted_via_rpc) {
      cobs_printf("booted_via_rpc is true: waiting 3 seconds for tether USB...\n");
      SleepMillis(coro_self, 3000);
  }

  if (boot_mode != 27 ) {  // Source RC files, unless BREAK KEY (also known as ESCAPE)
      // First SOURCE /rc/init.tcl
      int init_result = Tcl_Eval(global_tcl_interp, const_cast<char*>("source /rc/init.tcl"), 0, (char**)0);
      if (init_result == TCL_ERROR) {
        const char* output = global_tcl_interp->result;
        if (output) {
          tcl_io::emit('?');
          tcl_io::emit_string(output);
          tcl_io::emit('\n');
        }
      }

      // Then SOURCE /rc/mode%d.tcl
      char cmd_buf[40];
      sprintf(cmd_buf, "source /rc/mode%d.tcl", boot_mode);
      int mode_result = Tcl_Eval(global_tcl_interp, cmd_buf, 0, (char**)0);
      if (mode_result == TCL_ERROR) {
        const char* output = global_tcl_interp->result;
        if (output) {
          tcl_io::emit('?');
          tcl_io::emit_string(output);
          tcl_io::emit('\n');
        }
      }
      if (mode_result == TCL_BYE) {
          tcl_io::emit_string("[BYE]\n");
          goto BYE;
      }
  }

  { // REPL

  bool coco2_welcomed = false;  // Have we printed the banner on CoCo2?

  // Tcl REPL — exits when user types "bye"
  static std::string history[20];
  static int history_count = 0;

  while (true) {
    // Wait for prompt condition?
    while (tcl_io::active_io == 0) {
      sleep_ms(100);
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
        if (i < line_len) {
          tcl_io::emit(line[i]);
        } else {
          tcl_io::emit(' ');
        }
        if (i == line_cursor) tcl_io::emit_string("\x1b[0m");
      }
      tcl_io::emit_string("\x1b[K"); // clear rest of line
      for (int i = 0; i < (line_len - line_cursor + 1); i++) tcl_io::emit(8);
    };

    redraw_line();

    // Read a line from any active input
    while (true) {
      if (!g_pending_injections.empty()) {
        pcb::RpcRequest req = g_pending_injections.front();
        g_pending_injections.erase(g_pending_injections.begin());

        std::string cmd = req.data;
        cobs_printf("\n[Injecting: %s]\n", cmd.c_str());
        
        int rc = Tcl_Eval(global_tcl_interp, (char*)cmd.c_str(), 0, (char**)0);
        
        pcb::RpcResponse resp;
        resp.serial = req.serial;
        resp.status = rc;
        if (global_tcl_interp->result) {
            resp.data = global_tcl_interp->result;
        }
        pico_rpc::send_response(resp);
        
        redraw_line();
        continue;
      }

      // Detect CoCo2 joining mid-session.
      // IO_COCO2 is set by DriveConsole on the foreground after PIA init.
      // BackgroundSpoonFeeder may start before IO_COCO2 is set, so we
      // poll for it here and print the CoCo2 banner when it appears.
      if ((tcl_io::active_io & tcl_io::IO_COCO2) && !coco2_welcomed) {
        // Print banner on the CoCo2 screen
        console::emit_char_string("COPICO CENTIPEDE CONSOLE\n");
        coco2_welcomed = true;
        redraw_line();

        // Prime the keyboard state so any currently held keys are ignored
        // and don't trigger as new keystrokes in the REPL.
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
      
      if (key == 128) {  // Up — history previous
        if (history_index > 0) {
          if (history_index == history_count) {
             line[line_len] = '\0';
             current_edit = line;
          }
          history_index--;
          strcpy(line, history[history_index].c_str());
          line_len = strlen(line);
          line_cursor = line_len;
          redraw_line();
        }
        continue;
      }
      if (key == 129) {  // Down — history next
        if (history_index < history_count) {
          history_index++;
          if (history_index == history_count) {
            strcpy(line, current_edit.c_str());
          } else {
            strcpy(line, history[history_index].c_str());
          }
          line_len = strlen(line);
          line_cursor = line_len;
          redraw_line();
        }
        continue;
      }
      if (key == 130) {  // Cursor Left (non-destructive)
        if (line_cursor > 0) {
          line_cursor--;
          redraw_line();
        }
        continue;
      }
      if (key == 131) {  // Cursor Right (non-destructive)
        if (line_cursor < line_len) {
          line_cursor++;
          redraw_line();
        }
        continue;
      }

      if (key == 13) {  // Enter
        tcl_io::emit('\r');
        tcl_io::emit_string("    TCL>");
        line[line_len] = '\0';
        tcl_io::emit_string(line);
        tcl_io::emit_string("\x1b[K\n");
        break;
      }
      if (key == 8 || key == 127) {  // Backspace or DEL
        if (line_cursor > 0) {
          // shift left
          for (int i = line_cursor - 1; i < line_len - 1; i++) {
            line[i] = line[i+1];
          }
          line_len--;
          line_cursor--;
          redraw_line();
        }
        continue;
      }
      if (key >= 0x20 && key < 128 && line_len < 254) {
        // insert char
        for (int i = line_len; i > line_cursor; i--) {
          line[i] = line[i-1];
        }
        line[line_cursor] = (char)key;
        line_len++;
        line_cursor++;
        redraw_line();
        continue;
      }
    }
    line[line_len] = '\0';

    if (line_len > 0) {
      // Save history
      if (history_count == 0 || history[history_count - 1] != line) {
        if (history_count < 20) {
          history[history_count++] = line;
        } else {
          for (int i = 0; i < 19; i++) history[i] = history[i+1];
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
  } // End REPL

BYE:
  if (tcl_io::active_io & tcl_io::IO_COCO2) {
        // Coco2 is active — tell foreground to exit DriveConsole
        // and launch Coco2 into Disk Basic.
        tcl_io::emit_string("Launching Coco2...\n");
        uint cmd = ((uint)BG2FG_EXIT_CONSOLE << 24);
        while (!bg2fg.push(cmd)) {
          sleep_ms(1);
        }
        // DriveConsole will clear IO_COCO2 on exit.
  } else {
        tcl_io::emit_string("Goodbye.\n");
  }
  cobs_printf("BackgroundSpoonFeeder: bye, returning to background.\n");
  return;  // Return to spoon_task
}  // BackgroundSpoonFeeder

}  // end namespace gspoon
#endif  // _GSPOON_H_
