#pragma once
// ports_cmd.h -- Tcl commands for choosing the I/O port placement, and the `bye` that checks it first.
//
//   ports                      the current profile (TFR4 or COCO)
//   ports COCO                 shortcut: set profile=COCO
//   ports show                 the whole configuration as flat key-value pairs ("profile COCO becker 0xFF48 ...")
//   ports set KEY VALUE        set one key: `profile` or a device name; VALUE is 0xFF48, $FF48, or `off`
//   ports unset KEY            remove a key (back to the profile's default)
//   ports clear                remove all keys (profile TFR4)
//   ports check                resolve the placement for the current engine; "ok", or an error naming the clash
//   enhance                    list the enhancement devices and their default addresses
//   enhance DEVICE ?BASE|off?  shortcut: place an enhancement at its default (or the given) base address
//   bye                        launch the engine -- but only if the placement is valid (see below)
//
// The configuration is RAM only and written by Tcl every boot (lib/devices/ports_config.h).  A placement that
// overlaps another device, or uses $FF7F, makes `bye` fail with a message and leaves the board at TCL>.
// Included by main.cpp after registry.h (FindEngine) and before background.h (which registers the commands).

static const char* CurrentEngineName() {
  const EngineSpec* spec = FindEngine(centipede_config.engine);
  return spec ? spec->ports_name : "turbo9os";    // the name ports.tsv uses; an unknown or empty name runs the built-in Turbo9OS
}

static int ports_check_now(Tcl_Interp* interp, const char* who) {
  PortsCheck pc = PortsValidate(CurrentEngineName(), g_port_settings, g_io_scratch);
  if (pc.ok) return TCL_OK;
  Tcl_AppendResult(interp, who, ": ", CurrentEngineName(), " on ", g_port_settings.Profile(), ": ", pc.msg, (char*)NULL);
  return TCL_ERROR;
}

static bool ports_value_ok(const char* v) { uint16_t a; return !strcmp(v, "off") || ParsePortAddr(v, &a); }

static int ports_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  if (argc == 1) { Tcl_SetResult(interp, (char*)g_port_settings.Profile(), TCL_VOLATILE); return TCL_OK; }
  const char* sub = argv[1];
  if (!strcmp(sub, "show")) {
    Tcl_AppendElement(interp, (char*)"profile", 0); Tcl_AppendElement(interp, (char*)g_port_settings.Profile(), 0);
    for (int i = 0; i < g_port_settings.n_; i++) {
      if (!strcmp(g_port_settings.key_[i], "profile")) continue;
      Tcl_AppendElement(interp, g_port_settings.key_[i], 0); Tcl_AppendElement(interp, g_port_settings.val_[i], 0);
    }
    return TCL_OK;
  }
  if (!strcmp(sub, "check")) {
    if (ports_check_now(interp, "ports") != TCL_OK) return TCL_ERROR;
    Tcl_SetResult(interp, (char*)"ok", TCL_STATIC);
    return TCL_OK;
  }
  if (!strcmp(sub, "clear")) { g_port_settings.Clear(); return TCL_OK; }
  if (!strcmp(sub, "unset")) {
    if (argc != 3) { Tcl_AppendResult(interp, "usage: ports unset KEY", (char*)NULL); return TCL_ERROR; }
    g_port_settings.Unset(argv[2]);
    return TCL_OK;
  }
  if (!strcmp(sub, "set")) {
    if (argc != 4) { Tcl_AppendResult(interp, "usage: ports set KEY VALUE", (char*)NULL); return TCL_ERROR; }
    if (!strcmp(argv[2], "profile")) {
      if (!IsKnownProfile(argv[3])) { Tcl_AppendResult(interp, "unknown profile \"", argv[3], "\" (known: TFR4, COCO)", (char*)NULL); return TCL_ERROR; }
    } else if (!ports_value_ok(argv[3])) {
      Tcl_AppendResult(interp, "\"", argv[3], "\" is not an address like 0xFF48 (or off)", (char*)NULL); return TCL_ERROR;
    }
    if (!g_port_settings.Set(argv[2], argv[3])) { Tcl_AppendResult(interp, "too many settings, or a name that is too long", (char*)NULL); return TCL_ERROR; }
    return TCL_OK;
  }
  if (argc == 2) {                        // `ports COCO`
    if (!IsKnownProfile(sub)) { Tcl_AppendResult(interp, "unknown profile or subcommand \"", sub, "\": use TFR4, COCO, show, set, unset, clear or check", (char*)NULL); return TCL_ERROR; }
    g_port_settings.Set("profile", sub);
    return TCL_OK;
  }
  Tcl_AppendResult(interp, "usage: ports ?PROFILE|show|set KEY VALUE|unset KEY|clear|check?", (char*)NULL);
  return TCL_ERROR;
}

static int enhance_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  if (argc == 1) {                         // list: name first-last
    for (size_t i = 0; i < kNumPorts; i++) {
      if (strcmp(kPorts[i].profile, "E1")) continue;
      char buf[48];
      snprintf(buf, sizeof buf, "%s 0x%04X-0x%04X", kPorts[i].device, kPorts[i].first, kPorts[i].last);
      Tcl_AppendElement(interp, buf, 0);
    }
    return TCL_OK;
  }
  if (argc > 3) { Tcl_AppendResult(interp, "usage: enhance ?DEVICE ?BASE|off??", (char*)NULL); return TCL_ERROR; }
  const PortRange* e = nullptr;
  for (size_t i = 0; i < kNumPorts; i++)
    if (!strcmp(kPorts[i].profile, "E1") && !strcmp(kPorts[i].device, argv[1])) e = &kPorts[i];
  if (!e) { Tcl_AppendResult(interp, "\"", argv[1], "\" is not an enhancement device (try `enhance` for the list)", (char*)NULL); return TCL_ERROR; }
  char dflt[16];
  const char* val = dflt;
  snprintf(dflt, sizeof dflt, "0x%04X", e->first);
  if (argc == 3) {
    if (!ports_value_ok(argv[2])) { Tcl_AppendResult(interp, "\"", argv[2], "\" is not an address like 0xFF48 (or off)", (char*)NULL); return TCL_ERROR; }
    val = argv[2];
  }
  if (!g_port_settings.Set(argv[1], val)) { Tcl_AppendResult(interp, "too many settings", (char*)NULL); return TCL_ERROR; }
  return TCL_OK;
}

// `bye` for this product: like the library's, but the placement must be valid first.
static int bye_checked_cmd(ClientData, Tcl_Interp* interp, int argc, char** argv) {
  if (ports_check_now(interp, "bye") != TCL_OK) return TCL_ERROR;   // stay at TCL>; nothing was touched
  if (argc > 1) Tcl_SetResult(interp, argv[1], TCL_VOLATILE);
  return TCL_BYE;
}

static void register_ports_commands(Tcl_Interp* interp) {
  Tcl_CreateCommand(interp, (char*)"ports", ports_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"enhance", enhance_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"bye", bye_checked_cmd, NULL, NULL);   // replaces the library's bye
}
