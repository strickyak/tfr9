#pragma once
// ports_cmd.h -- Tcl commands `on` / `run`, `ports list|check`, and the `bye` that validates before launch.
//
//   on <host> run <os> ?for <target>? ?with ...?   compile target + options into flat lowercase key-value pairs
//   run <os> ?for <target>? ?with ...?             same, keeping the active `on <host>` (default: tfr911h)
//   on ?<host>?                                    query or set the active `on <host>`
//   ports list                                     list the compiled flat key-value pairs
//   ports check                                    validate the configuration ("ok", or an error naming the clash)
//   bye                                            validate (including host & launcher) and start the 6309
//
// Uppercase in `on`, `run` and `ports` commands is converted to lowercase (spec 15.y).
// Included by main.cpp after registry.h (FindLauncher) and before background.h.

static constexpr const char* kBoardHost = "tfr911h";

static int ports_check_now(Tcl_Interp* interp, const char* who, bool for_launch) {
  const char* legacy_eng = Tcl_GetVar2(interp, (char*)"Config", (char*)"engine", TCL_GLOBAL_ONLY);
  if (legacy_eng && legacy_eng[0]) {
    Tcl_AppendResult(interp, who, ": Config(engine) is no longer supported; use \"on ",
                     kBoardHost, " run <os> for <target>\"", (char*)NULL);
    return TCL_ERROR;
  }
  const TargetSpec* tgt = nullptr;
  PortsCheck trc = ResolveActiveTarget(g_port_settings, &tgt, kBoardHost);
  if (!trc.ok || !tgt) {
    Tcl_AppendResult(interp, who, ": ", trc.msg, (char*)NULL);
    return TCL_ERROR;
  }
  PortsCheck pc = PortsValidate(g_port_settings, g_io_scratch, /*include_host_reserved=*/true, kBoardHost);
  if (!pc.ok) {
    Tcl_AppendResult(interp, who, ": ", tgt->on_host, "/", tgt->run_os, "/", tgt->for_hw, ": ", pc.msg, (char*)NULL);
    return TCL_ERROR;
  }
  if (for_launch) {
    if (strcmp(tgt->on_host, kBoardHost) != 0) {
      Tcl_AppendResult(interp, who, ": configuration is \"on ", tgt->on_host,
                       "\", but this hardware is \"", kBoardHost, "\"", (char*)NULL);
      return TCL_ERROR;
    }
    if (!FindLauncher(tgt->run_os, tgt->for_hw) || !FindHamsterEngine(tgt->engine)) {
      Tcl_AppendResult(interp, who, ": target \"", tgt->run_os, " for ", tgt->for_hw,
                       "\" is not implemented in this firmware yet", (char*)NULL);
      return TCL_ERROR;
    }
    // Ensure default boot (when `on`/`run` was never called) has explicit keys before setup/IoFinish run.
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

// Sync any Tcl `Config(...)` image/drive paths into `g_config` at `bye` so users who write
// `set Config(drive0) /pc/foo.dsk; bye` without `menu store Config` get what they asked for.
static void sync_tcl_config_paths(Tcl_Interp* interp) {
  auto sync_one = [&](const char* key, char* dst, size_t cap) {
    const char* v = Tcl_GetVar2(interp, (char*)"Config", (char*)key, TCL_GLOBAL_ONLY);
    if (v) {
      strncpy(dst, v, cap - 1);
      dst[cap - 1] = 0;
    }
  };
  sync_one("flat_img", g_config.flat_img, sizeof(g_config.flat_img));
  sync_one("drive0",   g_config.drive0,   sizeof(g_config.drive0));
  sync_one("drive1",   g_config.drive1,   sizeof(g_config.drive1));
  sync_one("drive2",   g_config.drive2,   sizeof(g_config.drive2));
  sync_one("drive3",   g_config.drive3,   sizeof(g_config.drive3));
  sync_one("task1",    g_config.task1,    sizeof(g_config.task1));
  sync_one("task2",    g_config.task2,    sizeof(g_config.task2));
}

// `bye` for this product: validates target, I/O map, host hardware, and launcher before starting the 6309.
static int bye_checked_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  if (ports_check_now(interp, "bye", /*for_launch=*/true) != TCL_OK) return TCL_ERROR;
  sync_tcl_config_paths(interp);
  const char* run_os = g_port_settings.Get("run");
  if (run_os && !strcmp(run_os, "turbolab")) {
    const char* img = g_port_settings.Get("image");
    if (!img || !img[0]) img = g_config.flat_img[0] ? g_config.flat_img : nullptr;
    if (img && img[0]) {
      struct vfs_info st;
      if (vfs_stat(img, &st) != 0 || st.type == LFS_TYPE_DIR) {
        Tcl_AppendResult(interp, "bye: turbolab cannot open image \"", img, "\"", (char*)NULL);
        return TCL_ERROR;
      }
    }
  }
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
