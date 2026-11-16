#pragma once
// registry.h -- the `(run, for)` targets the TFR911H firmware can launch on core 0.
//
// `bye` and `ports check` resolve `(on, run, for)` via `ports_config.h` (`g_port_settings`) and look up the core-0
// setup function here; an unimplemented `(run, for)` target is refused cleanly at `bye`.
//
// Included by main.cpp after all the engines/*.h headers, because it names their setup functions.
#include <cstring>

struct TargetLauncher {
  const char* run_os;
  const char* for_hw;
  void (*setup)();             // core-0 setup function run before launching core 1
};

static const TargetLauncher kLaunchers[] = {
  {"turbo9os",  "turbo9sim",   setup_turbo9os},
  {"nitros9l1", "flat65280",   setup_flat65280},
  {"nitros9l2", "deep65280",   setup_deep65280},
  {"nitros9l2", "jamesbrown",  setup_jamesbrown},
  {"nitros9l2", "coco3",       setup_nitros9l2_coco3},
  {"hatvan",    "gep9",        setup_gep9},
  {"hatvan",    "gep9big",     setup_gep9big},
  {"hatvan",    "coco2",       setup_hatvan_coco2},
  {"fuzix",     "multicomp09", setup_multicomp09},
};

static inline const TargetLauncher* FindLauncher(const char* run_os, const char* for_hw) {
  for (const TargetLauncher& t : kLaunchers) {
    if (!strcmp(run_os, t.run_os) && !strcmp(for_hw, t.for_hw)) return &t;
  }
  return nullptr;
}
