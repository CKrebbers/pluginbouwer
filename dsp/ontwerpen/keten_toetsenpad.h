// keten_toetsenpad.h — GEGENEREERD door tools/bouwpakket.mjs uit het ontwerp "Toetsenpad". Niet met de hand wijzigen:
// verander het ontwerp in de tool en exporteer opnieuw. De rij hieronder is wat de ketenmotor (pb_graaf.h) leest,
// in de browser en in de plugin hetzelfde.
#pragma once
#include "../pb_core.h"

#define PB_NAAM "PB Toetsenpad"
#define PB_CLAP_ID "nl.varve.pluginbouwer.toetsenpad"
#define PB_VERSIE "0.1.0"
#define PB_MAKER "Varve"
#define PB_UITLEG "Zelf spelen: twee zaagtanden een octaaf uit elkaar, een filter dat per noot opent, chorus en galm. Speel met a s d f g h j k l ; ' of met de knoppen onder Horen."
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PTgg"
#define PB_INSTRUMENT 1
#define PB_AU_TYPE "aumu"
#define PB_NOTEN 1
#define PB_NOOT_PARAM 0
#define PB_POORT_PARAM 1
#define PB_LATENTIE 0
#define PB_N_PARAMS 4
#define PB_PARAMS \
    {1, "Noot", "", "×", 0.25, 4, 1, 5, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {2, "Poort", "", "", 0, 1, 0, 0, pb::AUTOMATEERBAAR}, \
    {3, "Filter", "", "Hz", 100, 8000, 900, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {4, "Loslaten", "", "ms", 10, 4000, 450, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}

static constexpr double PB_KETEN[] = {
  13, 4, 16, -1, 0, -1, 0, 0, 1,
  -1, 0, 0, 1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 17, 0, 0, -1, 0, -1, 12,
  -1, 0, 0, -1, 400, -1, 0, 0, -1,
  0.625, -1, 0, 0, 3, 450, -1, 0, 0,
  0, 0, 13, 0, 1, -1, 0, -1, 110,
  -1, 0, 0, -1, 0.5, -1, 0, 0, -1,
  1, -1, 0, 0, -1, 6, -1, 0, 0,
  1, 0, 13, 0, 1, -1, 0, -1, 220,
  -1, 0, 0, -1, 0.5, -1, 0, 0, -1,
  1, -1, 0, 0, -1, 0, -1, 0, 0,
  1, 0, 2, 3, 0, 2, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 7, 4, 0, -1, 0, 2, 900,
  1, 8, 1.5804820237218407, -1, 1.5, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 20, 5, 0, 1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 22, 6, 0, -1, 0, -1, 12,
  -1, 0, 0, -1, 4, -1, 0, 0, -1,
  0.5, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 9, 6, 0, 7, 0, -1, 0.5,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 21, 8, 0, -1, 0, -1, 2.5,
  -1, 0, 0, -1, 6000, -1, 0, 0, -1,
  1, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 9, 8, 0, 9, 0, -1, 0.25,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 3, 10, 0, -1, 0, -1, 0.34999999999999998,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 1, 11, 0, -1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 0.25, 4, 1, 5, 0, 1, 0,
  0, 100, 8000, 900, 20, 10, 4000, 450, 20
};
static constexpr int PB_KETEN_N = 369;

#include "../graaf_ontwerp.h"
