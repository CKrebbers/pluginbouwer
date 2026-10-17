// fabriekspresets.h — de fabriekspresets uit presets/*.json, in de plugin gebakken (ARCHITECTUUR.md "Presets").
// CMake maakt uit elk JSON-bestand één regel in fabriekspresets.inc (sleutel = bestandsnaam zonder .json); dit bestand
// controleert ze bij het compileren tegen PARAMS en biedt ze aan als CLAP preset-discovery-factory. De presets zitten
// in de plugin zelf (locatie van soort CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, location NULL; er zijn geen bestanden).
// Elke preset heeft een eigen provider met één locatie en één preset (een container van één, load_key = sleutel).
// Waarom niet één provider met alle presets in één container: clap-validator 0.4.1 (en master, okt 2026) blijft dan
// hangen. Zijn metadata-ontvanger houdt in begin_preset de mutex `result` vast en roept bij de tweede preset met een
// load_key flush_preset aan, dat dezelfde mutex nog eens neemt (src/plugin/preset_discovery/metadata_receiver.rs).
// Met één preset per container komt hij daar niet; voor een host is het even geldig. Zie VERSLAG.md.
// Laden gaat via de preset-load-extensie in dub_echo.cpp; clap-wrapper maakt er AU factory presets van (patch
// clap-wrapper-auv2-presets-uit-preset-discovery.patch). Alleen voor dub_echo.cpp: alles staat in een naamloze
// namespace.
#pragma once
#include <clap/clap.h>

#include <cstring>

#include "ontwerpen/keten_dub_echo.h"

namespace fabriek {
namespace {

using pb::Ontwerp;

struct Waarde {
  const char* param;  // naam zoals in PARAMS (en in de JSON)
  double v;
};
struct Preset {
  const char* sleutel;  // load_key: de bestandsnaam zonder .json
  const char* naam;
  const char* uitleg;
  int n;
  Waarde w[Ontwerp::N_PARAMS];
  clap_preset_discovery_provider_descriptor_t provider;
};

// één regel per JSON-bestand: PB_FABRIEKSPRESET(sleutel, naam, uitleg, aantal, {param, waarde}, ...)
#define PB_FABRIEKSPRESET(sleutel, naam, uitleg, n, ...)                                                        \
  {sleutel, naam, uitleg, n, {__VA_ARGS__},                                                                     \
   {CLAP_VERSION_INIT, PB_CLAP_ID ".fabriekspreset." sleutel, PB_NAAM " fabriekspreset " naam, PB_MAKER}},
constexpr Preset PRESETS[] = {
#include "fabriekspresets.inc"
};
#undef PB_FABRIEKSPRESET
constexpr int AANTAL = sizeof(PRESETS) / sizeof(PRESETS[0]);

constexpr bool gelijk(const char* a, const char* b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}
constexpr int paramIndex(const char* naam) {
  for (int i = 0; i < Ontwerp::N_PARAMS; ++i)
    if (gelijk(Ontwerp::PARAMS[i].naam, naam)) return i;
  return -1;
}
// elke naam bestaat, elke waarde ligt binnen het bereik, geen parameter twee keer, geen sleutel twee keer
constexpr bool kloppen() {
  for (int k = 0; k < AANTAL; ++k) {
    const Preset& p = PRESETS[k];
    for (int j = 0; j < k; ++j)
      if (gelijk(PRESETS[j].sleutel, p.sleutel) || gelijk(PRESETS[j].naam, p.naam)) return false;
    if (p.n < 1 || p.n > Ontwerp::N_PARAMS) return false;
    for (int a = 0; a < p.n; ++a) {
      const int i = paramIndex(p.w[a].param);
      if (i < 0 || !(p.w[a].v >= Ontwerp::PARAMS[i].min && p.w[a].v <= Ontwerp::PARAMS[i].max)) return false;
      for (int b = 0; b < a; ++b)
        if (gelijk(p.w[a].param, p.w[b].param)) return false;
    }
  }
  return AANTAL > 0;
}
static_assert(kloppen(), "presets/*.json: onbekende parameternaam, waarde buiten bereik, of een naam/sleutel dubbel");

const Preset* zoek(const char* sleutel) {
  if (!sleutel) return nullptr;
  for (const Preset& p : PRESETS)
    if (!std::strcmp(p.sleutel, sleutel)) return &p;
  return nullptr;
}

// --- preset-discovery: per preset een provider met één locatie (in de plugin) en één preset met load_key -----------
struct Provider {
  clap_preset_discovery_provider_t clap;
  const clap_preset_discovery_indexer_t* indexer;
  const Preset* preset;
};

bool providerInit(const clap_preset_discovery_provider_t* p) {
  auto* ix = static_cast<Provider*>(p->provider_data)->indexer;
  const clap_preset_discovery_location_t plek = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Fabriekspresets",
                                                 CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
  return ix->declare_location(ix, &plek);
}
void providerWeg(const clap_preset_discovery_provider_t* p) { delete static_cast<Provider*>(p->provider_data); }
bool providerMetadata(const clap_preset_discovery_provider_t* prov, uint32_t soort, const char* plek,
                      const clap_preset_discovery_metadata_receiver_t* r) {
  if (soort != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || plek) return false;
  const Preset& p = *static_cast<Provider*>(prov->provider_data)->preset;
  const clap_universal_plugin_id_t id = {"clap", PB_CLAP_ID};
  if (!r->begin_preset(r, p.naam, p.sleutel)) return true;
  r->add_plugin_id(r, &id);
  r->set_flags(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
  r->add_creator(r, PB_MAKER);
  if (p.uitleg[0]) r->set_description(r, p.uitleg);
  return true;
}
const void* providerExtensie(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

uint32_t factoryAantal(const clap_preset_discovery_factory_t*) { return AANTAL; }
const clap_preset_discovery_provider_descriptor_t* factoryDescriptor(const clap_preset_discovery_factory_t*,
                                                                     uint32_t i) {
  return i < (uint32_t)AANTAL ? &PRESETS[i].provider : nullptr;
}
const clap_preset_discovery_provider_t* factoryMaak(const clap_preset_discovery_factory_t*,
                                                     const clap_preset_discovery_indexer_t* indexer,
                                                     const char* id) {
  if (!indexer || !id) return nullptr;
  for (const Preset& f : PRESETS) {
    if (std::strcmp(id, f.provider.id) != 0) continue;
    auto* p = new Provider{{&f.provider, nullptr, providerInit, providerWeg, providerMetadata, providerExtensie},
                           indexer, &f};
    p->clap.provider_data = p;
    return &p->clap;
  }
  return nullptr;
}
const clap_preset_discovery_factory_t FACTORY = {factoryAantal, factoryDescriptor, factoryMaak};

}  // namespace
}  // namespace fabriek
