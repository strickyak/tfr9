#pragma once
// enhancements.h -- Phase 5 enhancement installer, reset, background service, and Tcl commands.
//
// Included after all device headers (`rtc_port.h`, `audio_sn76489.h`, `orchestra90.h`,
// `becker.h`, `info_port.h`, `gime.h`) so `IoFinish()` can install any user-requested
// `with ...` enhancement devices (`becker`, `audio`, `rtc`, `orchestra90`, `info`).

inline void IoResetEnhancementState() {
  g_rtc_active = false;
  audio::installed = false;
  becker::active = false;
  g_info_active = false;
  g_gime_mmu_enabled = true;
  g_gime_irq_enabled = false;
  g_gime_vbord_enabled = false;
  g_gime_vbord_pending = false;
}

inline void IoInstallEnhancementDevice(const char* dev, uint16_t base, uint16_t last, const TargetSpec& tgt) {
  if (!strcmp(dev, "becker")) {
    BeckerPort::Install(base);
  } else if (!strcmp(dev, "audio")) {
    AudioSn76489::Install(base);
  } else if (!strcmp(dev, "rtc")) {
    RtcPort::Install(base);
  } else if (!strcmp(dev, "orchestra90")) {
    Orchestra90::Install(base);
  } else if (!strcmp(dev, "info")) {
    InfoPort::Install(base);
  } else {
    IoClaim(base, last, dev);
    cobs_printf("[io-map: %s reserved at 0x%04x-0x%04x for %s/%s: no handler in this firmware yet]\n",
                dev, base, last, tgt.run_os, tgt.for_hw);
    return;
  }
  cobs_printf("[io-map: enhancement %s installed at 0x%04x-0x%04x]\n", dev, base, last);
}

inline void Phase2PollEnhancements() {
  rtc_poll_walltime();
  if (becker::active) becker::Pump();
  if (g_rtc_active) rtc_refresh_regs();
  if (g_info_active) info_service();
}

inline void RegisterEnhancementTclCommands(Tcl_Interp* interp) {
  Tcl_CreateCommand(interp, (char*)"beep", beep_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"click", click_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"fujinet", fujinet_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"rp2350b", rp2350b_cmd, NULL, NULL);
}
