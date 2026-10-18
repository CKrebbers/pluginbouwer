// presets.h — de fabriekspresets (presets/*.json, door CMake in het binary gebakken: presets_tabel.h).
// Een preset noemt parameters bij hun naam in PARAMS; een naam die er niet staat houdt zijn standaard.
// Alles hier is constexpr (valkuil 16: geen globals die pas bij de statische initialisatie gevuld worden).
#pragma once
#include <clap/clap.h>

#include "ontwerpen/keten_dub_echo.h"

namespace dub {

struct PresetWaarde {
  const char* naam;  // de naam in PARAMS
  double waarde;
};
struct Preset {
  const char* sleutel;  // bestandsnaam zonder .json: de load key
  const char* naam;
  const char* uitleg;
  const PresetWaarde* waarden;
  int n;
  const char* aanbieder;      // id van de preset-discovery-provider van deze preset
  const char* aanbiederNaam;  // zijn naam
};

constexpr bool zelfdeTekst(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
// index in PARAMS bij een naam, −1 als hij er niet is
constexpr int paramBijNaam(const char* naam) {
  for (int i = 0; i < pb::Ontwerp::N_PARAMS; ++i)
    if (zelfdeTekst(pb::Ontwerp::PARAMS[i].naam, naam)) return i;
  return -1;
}
// elke naam bestaat, staat er één keer in en elke waarde ligt in [min, max] (de bouw faalt anders: presets_tabel.h)
constexpr bool presetGeldig(const Preset& p) {
  for (int k = 0; k < p.n; ++k) {
    const int i = paramBijNaam(p.waarden[k].naam);
    if (i < 0) return false;
    const double v = p.waarden[k].waarde;
    if (!(v >= pb::Ontwerp::PARAMS[i].min && v <= pb::Ontwerp::PARAMS[i].max)) return false;
    for (int j = 0; j < k; ++j)
      if (zelfdeTekst(p.waarden[j].naam, p.waarden[k].naam)) return false;
  }
  return true;
}

// de preset bij een load key, of nullptr
const Preset* presetBijSleutel(const char* sleutel);
// alle waarden van een preset, per index in PARAMS (ontbrekende namen: hun standaard)
void presetWaarden(const Preset& p, double uit[pb::Ontwerp::N_PARAMS]);

// de preset-discovery-factory (CLAP_PRESET_DISCOVERY_FACTORY_ID): per preset een provider met één locatie van
// soort PLUGIN en een container van één preset (valkuil 15: clap-validator 0.4.1 loopt vast op een container met meer)
const clap_preset_discovery_factory_t* presetFabriek();

}  // namespace dub
