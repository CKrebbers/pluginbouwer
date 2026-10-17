// presets.h — de fabriekspresets uit presets/*.json als tabel (fase 4). CMake maakt de tabel bij het configureren
// (build/presets.cpp, niet bewerken); dub_echo.cpp biedt ze aan via de CLAP preset-discovery-factory en de
// preset-load-extensie, en clap-wrapper maakt er AU factory presets van.
#pragma once

namespace pb_presets {

struct Waarde {
  const char* param;  // de naam van de parameter, zoals in PARAMS (SPEC.md)
  double waarde;      // in de eigen eenheid van de parameter
};

struct Preset {
  const char* sleutel;  // de bestandsnaam zonder .json: de load_key, verandert nooit
  const char* naam;     // wat de host toont
  const char* uitleg;
  const Waarde* waarden;
  unsigned aantal;
};

// "standaard" eerst (AU-presetnummer 0), de rest op bestandsnaam
extern const Preset LIJST[];
extern const unsigned AANTAL;

}  // namespace pb_presets
