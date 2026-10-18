// presets.cpp — de fabriekspresets als CLAP preset-discovery (ARCHITECTUUR.md "Presets").
// Per preset een provider met één locatie van soort CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN (location = nullptr) en
// daarin een container van één preset; de load key is de sleutel (bestandsnaam). Laden gaat via preset-load in
// dub_echo.cpp. De AU leest dezelfde factory (patches/clap-wrapper-auv2-presets.patch) voor zijn factory presets.
#include "presets.h"

#include <array>
#include <cstring>
#include <utility>

#include "presets_tabel.h"

namespace dub {
namespace {

static_assert(N_PRESETS > 0, "geen fabriekspresets");

template <size_t... I>
constexpr std::array<clap_preset_discovery_provider_descriptor_t, N_PRESETS> maakBeschrijvingen(
    std::index_sequence<I...>) {
  return {{{CLAP_VERSION_INIT, PRESETS[I].aanbieder, PRESETS[I].aanbiederNaam, PB_MAKER}...}};
}
// constant geïnitialiseerd (valkuil 16)
constexpr std::array<clap_preset_discovery_provider_descriptor_t, N_PRESETS> BESCHRIJVINGEN =
    maakBeschrijvingen(std::make_index_sequence<N_PRESETS>{});

constexpr clap_preset_discovery_location_t LOCATIE = {CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Fabriekspresets",
                                                      CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};

struct Aanbieder {
  clap_preset_discovery_provider_t clap{};
  const clap_preset_discovery_indexer_t* indexer = nullptr;
  int k = 0;  // index in PRESETS
};
const Aanbieder* van(const clap_preset_discovery_provider_t* p) {
  return static_cast<const Aanbieder*>(p->provider_data);
}

bool aanbiederInit(const clap_preset_discovery_provider_t* p) {
  const Aanbieder* a = van(p);
  return a->indexer->declare_location(a->indexer, &LOCATIE);
}
void aanbiederWeg(const clap_preset_discovery_provider_t* p) { delete van(p); }
bool aanbiederMetadata(const clap_preset_discovery_provider_t* p, uint32_t soort, const char* locatie,
                       const clap_preset_discovery_metadata_receiver_t* r) {
  if (soort != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || locatie != nullptr || !r) return false;
  const Preset& ps = PRESETS[van(p)->k];
  if (!r->begin_preset(r, ps.naam, ps.sleutel)) return true;  // de host wil hem niet: geen fout
  const clap_universal_plugin_id_t id = {"clap", PB_CLAP_ID};
  r->add_plugin_id(r, &id);
  r->set_flags(r, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
  r->add_creator(r, PB_MAKER);
  if (ps.uitleg && ps.uitleg[0]) r->set_description(r, ps.uitleg);
  return true;
}
const void* aanbiederExtensie(const clap_preset_discovery_provider_t*, const char*) { return nullptr; }

uint32_t fabAantal(const clap_preset_discovery_factory_t*) { return N_PRESETS; }
const clap_preset_discovery_provider_descriptor_t* fabBeschrijving(const clap_preset_discovery_factory_t*,
                                                                  uint32_t i) {
  return i < (uint32_t)N_PRESETS ? &BESCHRIJVINGEN[i] : nullptr;
}
const clap_preset_discovery_provider_t* fabMaak(const clap_preset_discovery_factory_t*,
                                                const clap_preset_discovery_indexer_t* indexer, const char* id) {
  if (!indexer || !id) return nullptr;
  for (int k = 0; k < N_PRESETS; ++k) {
    if (std::strcmp(id, BESCHRIJVINGEN[k].id) != 0) continue;
    auto* a = new Aanbieder();
    a->indexer = indexer;
    a->k = k;
    a->clap = {&BESCHRIJVINGEN[k], a, aanbiederInit, aanbiederWeg, aanbiederMetadata, aanbiederExtensie};
    return &a->clap;
  }
  return nullptr;
}
constexpr clap_preset_discovery_factory_t FABRIEK = {fabAantal, fabBeschrijving, fabMaak};

}  // namespace

const Preset* presetBijSleutel(const char* sleutel) {
  if (!sleutel) return nullptr;
  for (int k = 0; k < N_PRESETS; ++k)
    if (!std::strcmp(PRESETS[k].sleutel, sleutel)) return &PRESETS[k];
  return nullptr;
}

void presetWaarden(const Preset& p, double uit[pb::Ontwerp::N_PARAMS]) {
  for (int i = 0; i < pb::Ontwerp::N_PARAMS; ++i) uit[i] = pb::Ontwerp::PARAMS[i].standaard;
  for (int k = 0; k < p.n; ++k) uit[paramBijNaam(p.waarden[k].naam)] = p.waarden[k].waarde;  // geldig: static_assert
}

const clap_preset_discovery_factory_t* presetFabriek() { return &FABRIEK; }

}  // namespace dub
