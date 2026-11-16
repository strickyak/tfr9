#pragma once
// repl.h -- Tcl REPL coroutine (core 0, before the 6309 starts).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// Tcl REPL coroutine (runs on core 0 before 6309 starts)
// ═══════════════════════════════════════════════════════════════════
static uint8_t tcl_stack[20 * 1024];  // 20K for Tcl REPL + VFS RPC
volatile bool tcl_repl_done = false;

static std::string repl_history[20];
static int repl_history_count = 0;

static void tcl_repl_task(Coro& self) {
  rpc::g_vfs_coro = &self;  // VFS RPC yields back to scheduler
  gspoon::g_spoon_coro = &self;  // ls_cmd etc. call coro_yield(g_spoon_coro)
  cobs_printf("\nTFR911 TCL SHELL.  To exit TCL and launch 6809, type  bye \n");

  bool booted_via_rpc = false;
  if (::boot_mode_check == ::boot_mode + BOOT_MODE_CHECKER) {
    ::boot_mode_check = 0;
    booted_via_rpc = true;
  } else {
    ::boot_mode = 0;
  }
  cobs_printf("boot_mode=%u (booted_via_rpc=%d)\n", (unsigned)::boot_mode, booted_via_rpc ? 1 : 0);

  if (booted_via_rpc) {
    // v4 slept a fixed 3 s here.  USB re-enumeration plus the PC reopening the port takes a variable time;
    // if the tether was late, /pc/modeN.tcl could not be read, the warning below was dropped (nobody was
    // connected to hear it) and the board sat at TCL> forever (spec S53).  Wait until the host has opened
    // the port (DTR), up to 20 s, then give the tether a moment (0.3 s) to start its handlers.
    uint32_t waited_ms = 0;
    while (!usb_tether_ok() && waited_ms < 20000) {
      gspoon::SleepMillis(&self, 50);
      waited_ms += 50;
    }
    gspoon::SleepMillis(&self, 300);
    cobs_printf("booted_via_rpc: tether connected after %u ms\n", (unsigned)waited_ms);
  }

  if (boot_mode != 27) {  // Source RC files, unless BREAK (27 / ESC)
    // First SOURCE /rc/init.tcl if it exists
    struct vfs_info st;
    if (vfs_stat("/rc/init.tcl", &st) == 0) {
      cobs_printf("[Sourcing /rc/init.tcl]\n");
      int init_result = Tcl_Eval(global_tcl_interp, const_cast<char*>("source /rc/init.tcl"), 0, (char**)0);
      if (init_result == TCL_ERROR) {
        const char* output = global_tcl_interp->result;
        if (output && output[0]) cobs_printf("?%s\n", output);
      } else if (init_result == TCL_BYE) {
        cobs_printf("[BYE]\n");
        goto BYE;
      }
    }

    // Then SOURCE /rc/mode%u.tcl (fallback to /pc/mode%u.tcl if not in /rc)
    if (booted_via_rpc || boot_mode > 0) {
      char path_buf[40];
      snprintf(path_buf, sizeof(path_buf), "/rc/mode%u.tcl", (unsigned)boot_mode);
      if (vfs_stat(path_buf, &st) != 0) {
        snprintf(path_buf, sizeof(path_buf), "/pc/mode%u.tcl", (unsigned)boot_mode);
      }
      if (vfs_stat(path_buf, &st) == 0) {
        cobs_printf("[Sourcing %s]\n", path_buf);
        char cmd_buf[50];
        snprintf(cmd_buf, sizeof(cmd_buf), "source %s", path_buf);
        int mode_result = Tcl_Eval(global_tcl_interp, cmd_buf, 0, (char**)0);
        if (mode_result == TCL_ERROR) {
          const char* output = global_tcl_interp->result;
          if (output && output[0]) cobs_printf("?%s\n", output);
        } else if (mode_result == TCL_BYE) {
          cobs_printf("[BYE]\n");
          goto BYE;
        }
      } else {
        cobs_printf("[Warning: mode%u.tcl not found in /rc or /pc]\n", (unsigned)boot_mode);
      }
    }
  }

  char line[256];
  while (true) {
    cobs_printf("TCL> ");
    int line_len = 0;
    int line_cursor = 0;
    int history_index = repl_history_count;
    std::string current_edit = "";

    auto redraw_line = [&]() {
      cobs_printf("\rTCL> ");
      for (int i = 0; i <= line_len; i++) {
        if (i == line_cursor) cobs_printf("\x1b[7m");
        if (i < line_len) {
          cobs_putchar(line[i]);
        } else {
          cobs_putchar(' ');
        }
        if (i == line_cursor) cobs_printf("\x1b[0m");
      }
      cobs_printf("\x1b[K"); // clear rest of line
      for (int i = 0; i < (line_len - line_cursor + 1); i++) cobs_putchar(8);
    };

    // Read a line from USB (yield while waiting for keys)
    while (true) {
      // Check for injected commands from PicoRPC
      if (!g_pending_injections.empty()) {
        pcb::RpcRequest req = g_pending_injections.front();
        g_pending_injections.erase(g_pending_injections.begin());
        std::string cmd = req.data;
        cobs_printf("[Injecting: %s]\n", cmd.c_str());
        int rc = Tcl_Eval(global_tcl_interp, (char*)cmd.c_str(), 0, (char**)0);
        pcb::RpcResponse resp;
        resp.serial = req.serial;
        resp.status = rc;
        const char* out = global_tcl_interp->result;
        if (out && out[0]) {
          resp.message = out;
          resp.data = out;
        }
        pico_rpc::send_response(resp);
        if (rc == TCL_BYE) goto BYE;
        cobs_printf("TCL> ");
        continue;
      }

      // Poll for keyboard input from USB packets
      std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
        return s && s->length() > 0 &&
               (unsigned char)(*s)[0] != T_RPC &&
               (unsigned char)(*s)[0] != T_PICO_RPC &&
               (unsigned char)(*s)[0] != T_WALLTIME;
      });
      if (pkt) {
        byte ch = (byte)(*pkt)[0];
        if (ch == C_PUTCHAR && pkt->size() >= 2) ch = (byte)(*pkt)[1];
        delete pkt;
        if (ch == 13 || ch == 10) {  // End of line
          cobs_putchar('\r');
          cobs_printf("TCL> ");
          line[line_len] = '\0';
          cobs_printf("%s", line);
          cobs_printf("\x1b[K\n");
          break;
        }
        if (ch == 128) {  // Up — history previous
          if (history_index > 0) {
            if (history_index == repl_history_count) {
               line[line_len] = '\0';
               current_edit = line;
            }
            history_index--;
            strcpy(line, repl_history[history_index].c_str());
            line_len = strlen(line);
            line_cursor = line_len;
            redraw_line();
          }
          continue;
        }
        if (ch == 129) {  // Down — history next
          if (history_index < repl_history_count) {
            history_index++;
            if (history_index == repl_history_count) {
              strcpy(line, current_edit.c_str());
            } else {
              strcpy(line, repl_history[history_index].c_str());
            }
            line_len = strlen(line);
            line_cursor = line_len;
            redraw_line();
          }
          continue;
        }
        if (ch == 130) {  // Cursor Left (non-destructive)
          if (line_cursor > 0) {
            line_cursor--;
            redraw_line();
          }
          continue;
        }
        if (ch == 131) {  // Cursor Right (non-destructive)
          if (line_cursor < line_len) {
            line_cursor++;
            redraw_line();
          }
          continue;
        }
        if (ch == 8 || ch == 127) {  // Backspace
          if (line_cursor > 0) {
            for (int i = line_cursor - 1; i < line_len - 1; i++) {
              line[i] = line[i+1];
            }
            line_len--;
            line_cursor--;
            redraw_line();
          }
          continue;
        }
        if (ch > 127) {
          continue;  // Skip non-ASCII (like the ² superscript)
        }
        if (ch >= 0x20 && line_len < 254) {
          for (int i = line_len; i > line_cursor; i--) {
            line[i] = line[i-1];
          }
          line[line_cursor] = (char)ch;
          line_len++;
          line_cursor++;
          redraw_line();
        }
        continue;
      }
      coro_yield(&self);  // Nothing to do — yield to let PumpUsbCobs run
    }
    line[line_len] = 0;

    if (line_len == 0) continue;

    // Save history
    if (repl_history_count == 0 || repl_history[repl_history_count - 1] != line) {
      if (repl_history_count < 20) {
        repl_history[repl_history_count++] = line;
      } else {
        for (int i = 0; i < 19; i++) repl_history[i] = repl_history[i+1];
        repl_history[19] = line;
      }
    }

    int result;
    if (strchr(line, ';') == NULL && strchr(line, '[') == NULL &&
        strchr(line, ']') == NULL && strchr(line, '{') == NULL &&
        strchr(line, '}') == NULL && strchr(line, '"') == NULL) {
      char new_line[260];
      snprintf(new_line, sizeof(new_line), "fs %s", line);
      result = Tcl_Eval(global_tcl_interp, new_line, 0, (char**)0);
    } else {
      result = Tcl_Eval(global_tcl_interp, line, 0, (char**)0);
    }
    const char* output = global_tcl_interp->result;
    if (output && output[0]) {
      if (result == TCL_ERROR) cobs_putchar('?');
      tcl_io::emit_string(output);
      cobs_putchar('\n');
    }
    if (result == TCL_BYE) break;
  }

BYE:
  rpc::g_vfs_coro = nullptr;
  gspoon::g_spoon_coro = nullptr;
  cobs_printf("[BYE — starting 6309]\n");
  tcl_repl_done = true;
}

