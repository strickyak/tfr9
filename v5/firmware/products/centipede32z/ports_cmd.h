#pragma once
// ports_cmd.h -- Centipede 32z Tcl commands `on` / `run`, `ports list|check`, and checked `bye`.
//
// Supports both `on coco2` (default) and `on coco3` hosts, and honors `Config(become_coco3)` / `Config(fuzix)`
// from existing `/rc/mode*.tcl` scripts when no explicit `run` command was issued.

static constexpr const char* kBoardHost = "coco2";

static void apply_legacy_centipede_flags_if_unset() {
  if (g_port_settings.Get("run") != nullptr) return;
  if (g_config.fuzix) {
    g_port_settings.Set("on", "coco2");
    g_port_settings.Set("run", "fuzix");
    g_port_settings.Set("for", "coco2");
    g_port_settings.Set("engine", "gerbil_gime");
  } else if (g_config.become_coco3) {
    g_port_settings.Set("on", "coco2");
    g_port_settings.Set("run", "rsbasic");
    g_port_settings.Set("for", "coco3");
    g_port_settings.Set("engine", "gerbil_gime");
  }
}

static void sync_tcl_config_paths(Tcl_Interp* interp) {
  auto sync_str = [&](const char* key, char* dst, size_t cap) {
    const char* v = Tcl_GetVar2(interp, (char*)"Config", (char*)key, TCL_GLOBAL_ONLY);
    if (v) {
      strncpy(dst, v, cap - 1);
      dst[cap - 1] = 0;
    }
  };
  auto sync_bool = [&](const char* key, volatile bool& dst) {
    const char* v = Tcl_GetVar2(interp, (char*)"Config", (char*)key, TCL_GLOBAL_ONLY);
    if (v) dst = (atoi(v) != 0);
  };
  sync_bool("become_coco3", g_config.become_coco3);
  sync_bool("fuzix",        g_config.fuzix);
  sync_bool("ram_64k",      g_config.ram_64k);
  sync_bool("rom_disk11",   g_config.rom_disk11);
  sync_bool("floppy_fd",    g_config.floppy_fd);
  sync_bool("floppy_pc",    g_config.floppy_pc);
  sync_bool("trace_writes", g_config.trace_writes);
  sync_bool("trace_reads",  g_config.trace_reads);
  sync_str("flat_img",      g_config.flat_img,     sizeof(g_config.flat_img));
  sync_str("drive0",        g_config.drive0,       sizeof(g_config.drive0));
  sync_str("drive1",        g_config.drive1,       sizeof(g_config.drive1));
  sync_str("drive2",        g_config.drive2,       sizeof(g_config.drive2));
  sync_str("drive3",        g_config.drive3,       sizeof(g_config.drive3));
  sync_str("task1",         g_config.task1,        sizeof(g_config.task1));
  sync_str("task2",         g_config.task2,        sizeof(g_config.task2));
  sync_str("preload_decb",  g_config.preload_decb, sizeof(g_config.preload_decb));
}

static int ports_check_now(Tcl_Interp* interp, const char* who, bool for_launch) {
  if (interp) {
    const char* legacy_eng = Tcl_GetVar2(interp, (char*)"Config", (char*)"engine", TCL_GLOBAL_ONLY);
    if (legacy_eng && legacy_eng[0]) {
      Tcl_AppendResult(interp, who, ": Config(engine) is no longer supported; use \"on ",
                       kBoardHost, " run <os> for <target>\"", (char*)NULL);
      return TCL_ERROR;
    }
    sync_tcl_config_paths(interp);
  }
  apply_legacy_centipede_flags_if_unset();

  const TargetSpec* tgt = nullptr;
  PortsCheck trc = ResolveActiveTarget(g_port_settings, &tgt, kBoardHost);
  if (!trc.ok || !tgt) {
    if (interp) Tcl_AppendResult(interp, who, ": ", trc.msg, (char*)NULL);
    return TCL_ERROR;
  }
  PortsCheck pc = PortsValidate(g_port_settings, g_io_scratch, /*include_host_reserved=*/true, kBoardHost);
  if (!pc.ok) {
    if (interp) {
      Tcl_AppendResult(interp, who, ": ", tgt->on_host, "/", tgt->run_os, "/", tgt->for_hw, ": ", pc.msg, (char*)NULL);
    }
    return TCL_ERROR;
  }
  if (for_launch) {
    if (strcmp(tgt->on_host, "coco2") != 0 && strcmp(tgt->on_host, "coco3") != 0) {
      if (interp) {
        Tcl_AppendResult(interp, who, ": configuration is \"on ", tgt->on_host,
                         "\", but this hardware is a Centipede cartridge (\"coco2\" or \"coco3\")", (char*)NULL);
      }
      return TCL_ERROR;
    }
    if (!FindCentipedeLauncher(tgt->on_host, tgt->run_os, tgt->for_hw) || !FindGerbilEngine(tgt->engine)) {
      if (interp) {
        Tcl_AppendResult(interp, who, ": target \"", tgt->on_host, "/", tgt->run_os, " for ", tgt->for_hw,
                         "\" is not implemented in this firmware yet", (char*)NULL);
      }
      return TCL_ERROR;
    }
    if (!g_port_settings.Get("run")) {
      g_port_settings.Set("on", tgt->on_host);
      g_port_settings.Set("run", tgt->run_os);
      g_port_settings.Set("for", tgt->for_hw);
      g_port_settings.Set("engine", tgt->engine);
    }
  }
  return TCL_OK;
}

static int on_run_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  char cmd0[PortSettings::kLen];
  if (argc == 1 && LowerCopy(cmd0, sizeof cmd0, argv[0]) && !strcmp(cmd0, "on")) {
    Tcl_SetResult(interp, (char*)g_port_settings.Host(kBoardHost), TCL_VOLATILE);
    return TCL_OK;
  }
  PortsCheck rc = ParseOnRunCommand(argc, argv, kBoardHost, g_port_settings);
  if (!rc.ok) {
    Tcl_AppendResult(interp, rc.msg, (char*)NULL);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int ports_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  char sub[PortSettings::kLen];
  if (argc != 2 || !LowerCopy(sub, sizeof sub, argv[1])) {
    Tcl_AppendResult(interp, "usage: ports list|check", (char*)NULL);
    return TCL_ERROR;
  }
  if (!strcmp(sub, "list")) {
    if (g_port_settings.n_ == 0) {
      const TargetSpec* tgt = nullptr;
      if (ResolveActiveTarget(g_port_settings, &tgt, kBoardHost).ok && tgt) {
        Tcl_AppendElement(interp, (char*)"on", 0);     Tcl_AppendElement(interp, (char*)tgt->on_host, 0);
        Tcl_AppendElement(interp, (char*)"run", 0);    Tcl_AppendElement(interp, (char*)tgt->run_os, 0);
        Tcl_AppendElement(interp, (char*)"for", 0);    Tcl_AppendElement(interp, (char*)tgt->for_hw, 0);
        Tcl_AppendElement(interp, (char*)"engine", 0); Tcl_AppendElement(interp, (char*)tgt->engine, 0);
      }
      return TCL_OK;
    }
    for (int i = 0; i < g_port_settings.n_; i++) {
      Tcl_AppendElement(interp, g_port_settings.key_[i], 0);
      Tcl_AppendElement(interp, g_port_settings.val_[i], 0);
    }
    return TCL_OK;
  }
  if (!strcmp(sub, "check")) {
    if (ports_check_now(interp, "ports", /*for_launch=*/false) != TCL_OK) return TCL_ERROR;
    Tcl_SetResult(interp, (char*)"ok", TCL_STATIC);
    return TCL_OK;
  }
  Tcl_AppendResult(interp, "unknown subcommand \"", argv[1], "\": must be list or check", (char*)NULL);
  return TCL_ERROR;
}

static int bye_checked_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  if (ports_check_now(interp, "bye", /*for_launch=*/true) != TCL_OK) return TCL_ERROR;
  if (argc > 1) Tcl_SetResult(interp, argv[1], TCL_VOLATILE);
  return TCL_BYE;
}

static void register_ports_commands(Tcl_Interp* interp) {
  Tcl_CreateCommand(interp, (char*)"on", on_run_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"ON", on_run_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"run", on_run_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"RUN", on_run_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"ports", ports_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"PORTS", ports_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"bye", bye_checked_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"BYE", bye_checked_cmd, NULL, NULL);
}

inline bool g_centipede_launch_prepared = false;

// Execute the resolved target setup on Core 0 right before pushing BG2FG_EXIT_CONSOLE to Core 1!
static void centipede_prepare_launch_on_core0() {
  if (g_centipede_launch_prepared) return;
  g_centipede_launch_prepared = true;
  (void)ports_check_now(global_tcl_interp, "bye", /*for_launch=*/true);
  const TargetSpec* tgt = nullptr;
  if (!ResolveActiveTarget(g_port_settings, &tgt, kBoardHost).ok || !tgt) {
    tgt = FindTarget("coco2", "rsbasic", "coco2");
  }
  const CentipedeTargetLauncher* launcher =
      tgt ? FindCentipedeLauncher(tgt->on_host, tgt->run_os, tgt->for_hw) : nullptr;
  GerbilLoopFn loop_fn = tgt ? FindGerbilEngine(tgt->engine) : gerbil_sam64k_foreground;

  tcl_io::emit_string("Launching ");
  tcl_io::emit_string(tgt ? tgt->on_host : "coco2");
  tcl_io::emit_string("/");
  tcl_io::emit_string(tgt ? tgt->run_os : "rsbasic");
  tcl_io::emit_string(" for ");
  tcl_io::emit_string(tgt ? tgt->for_hw : "coco2");
  tcl_io::emit_string(" [");
  tcl_io::emit_string(tgt ? tgt->engine : "gerbil_sam64k");
  tcl_io::emit_string("]...\n");

  if (launcher && launcher->setup) {
    launcher->setup();
  } else {
    setup_centipede_rsbasic_coco2();
  }
  g_gerbil_loop_fn = loop_fn ? loop_fn : gerbil_sam64k_foreground;
  ResetCycleStreamForNewSession();
  __dmb();
}
