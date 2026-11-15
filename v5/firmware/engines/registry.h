#pragma once
// registry.h -- the engines the TFR911H firmware can launch, by canonical name.  Replaces the strcmp() chain that
// used to live in the background loop's `bye` handler (spec section 5.1: an engine = {name, setup}).
//
// `bye` and `ports check` look up Config(engine) here.  Only canonical names are accepted (aliases were removed on
// 2026-11-15 to avoid confusion); an empty Config(engine) means the built-in default "turbo9os", and any unknown
// non-empty name is refused at `bye` / `ports check`.
//
// Included by main.cpp after all the engines/*.h headers, because it names their setup functions.
#include <cstring>

struct EngineSpec {
  const char* name;            // canonical name (what `Config(engine)` and `ports.tsv` use)
  void (*setup)();             // nullptr: nothing to set up (Turbo9OS is built in)
};

static const EngineSpec kEngines[] = {
  {"turbo9os",         nullptr},
  {"flat65280",        setup_flat65280},
  {"deep65280",        setup_deep65280},
  {"gep9",             setup_gep9},
  {"gep9big",          setup_gep9big},
  {"multicomp09fuzix", setup_multicomp09},
};

// The spec for `name`, or nullptr if there is no such engine.
static inline const EngineSpec* FindEngine(const char* name) {
  for (const EngineSpec& e : kEngines) {
    if (strcmp(name, e.name) == 0) return &e;
  }
  return nullptr;
}
