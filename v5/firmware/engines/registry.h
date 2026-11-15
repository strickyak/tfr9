#pragma once
// registry.h -- the engines the TFR911H firmware can launch, by name.  Replaces the strcmp() chain that used to
// live in the background loop's `bye` handler (spec section 5.1: an engine = {name, aliases, setup}).
//
// `bye` looks up Config(engine) here.  A name that is not in the table (or "turbo9os") runs no setup function:
// the built-in Turbo9OS/Turbo9Sim is the default engine.  Aliases are the spellings v4 accepted (including the
// misspellings "multicomp09fuxiz" and "deep64280", kept so existing Config values keep working).
//
// Included by main.cpp after all the engines/*.h headers, because it names their setup functions.
#include <cstring>

struct EngineSpec {
  const char* name;            // canonical name (what `menu` shows)
  const char* ports_name;      // the name engines/ports.tsv uses for this engine (differs for Fuzix)
  const char* aliases[4];      // other accepted spellings, nullptr-terminated
  void (*setup)();             // nullptr: nothing to set up (Turbo9OS is built in)
};

static const EngineSpec kEngines[] = {
  {"turbo9os",  "turbo9os",    {nullptr},                                         nullptr},
  {"flat65280", "flat65280",   {"flat", nullptr},                                 setup_flat65280},
  {"deep65280", "deep65280",   {"deep64280", "deep", nullptr},                    setup_deep65280},
  {"gep9",      "gep9",        {"hatvan", nullptr},                               setup_gep9},
  {"gep9big",   "gep9big",     {"gep9+", "big6809", "hatvanbig", nullptr},        setup_gep9big},
  {"multicomp09fuzix", "multicomp09", {"multicomp09fuxiz", "multicomp09", "fuzix", nullptr}, setup_multicomp09},
};

// The spec for `name` (canonical or alias), or nullptr if there is no such engine.
static inline const EngineSpec* FindEngine(const char* name) {
  for (const EngineSpec& e : kEngines) {
    if (strcmp(name, e.name) == 0) return &e;
    for (int i = 0; e.aliases[i]; i++) {
      if (strcmp(name, e.aliases[i]) == 0) return &e;
    }
  }
  return nullptr;
}
