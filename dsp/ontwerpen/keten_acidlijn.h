// keten_acidlijn.h — GEGENEREERD door tools/bouwpakket.mjs uit het ontwerp "Acidlijn". Niet met de hand wijzigen:
// verander het ontwerp in de tool en exporteer opnieuw. De rij hieronder is wat de ketenmotor (pb_graaf.h) leest,
// in de browser en in de plugin hetzelfde.
#pragma once
#include "../pb_core.h"

#define PB_NAAM "PB Acidlijn"
#define PB_CLAP_ID "nl.varve.pluginbouwer.acidlijn"
#define PB_VERSIE "0.1.0"
#define PB_MAKER "Varve"
#define PB_UITLEG "Zaagtand door een resonerend filter dat per noot opengaat (de omhulling beweegt de afsnijding), en dan de band in."
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PAzb"
#define PB_INSTRUMENT 1
#define PB_AU_TYPE "aumu"
#define PB_LATENTIE 0
#define PB_N_PARAMS 4
#define PB_PARAMS \
    {1, "Afsnijding", "", "Hz", 60, 4000, 300, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {2, "Resonantie", "", "Q", 0.5, 8, 6, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {3, "Tempo", "", "BPM", 80, 180, 128, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {4, "Verzadiging", "", "dB", 0, 24, 9, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}

static constexpr double PB_KETEN[] = {
  8, 4, 15, -1, 0, -1, 0, 2, 128,
  -1, 0, 0, -1, 0.5, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  5, 1, 17, 0, 0, -1, 0, -1, 1,
  -1, 0, 0, -1, 180, -1, 0, 0, -1,
  0.125, -1, 0, 0, -1, 60, -1, 0, 0,
  0, 0, 13, 0, 1, -1, 0, -1, 55,
  -1, 0, 0, -1, 0.5, -1, 0, 0, -1,
  1, -1, 0, 0, -1, 0, -1, 0, 0,
  1, 0, 20, 2, 0, 1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 7, 3, 0, -1, 0, 0, 300,
  1, 8, 3.0294468445267841, 1, 6, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 4, 4, 0, -1, 0, 3, 9,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 3, 5, 0, -1, 0, -1, 1,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 1, 6, 0, -1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 60, 4000, 300, 20, 0.5, 8, 6,
  20, 80, 180, 128, 20, 0, 24, 9, 20
};
static constexpr int PB_KETEN_N = 234;

#include "../graaf_ontwerp.h"
