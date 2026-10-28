// keten_dub_echo.h — GEGENEREERD door tools/bouwpakket.mjs uit het ontwerp "Dub echo". Niet met de hand wijzigen:
// verander het ontwerp in de tool en exporteer opnieuw. De rij hieronder is wat de ketenmotor (pb_graaf.h) leest,
// in de browser en in de plugin hetzelfde.
#pragma once
#include "../pb_core.h"

#define PB_NAAM "PB Dub echo"
#define PB_CLAP_ID "nl.varve.pluginbouwer.dub-echo"
#define PB_VERSIE "0.1.0"
#define PB_MAKER "Varve"
#define PB_UITLEG "Tape echo voor dub: saturation en een filter in de feedbacklus. Boven feedback 1 gaat hij vanzelf door, en de tape houdt hem in toom."
#define PB_AU_MAKER "Vrve"
#define PB_AU_SUBTYPE "PDca"
#define PB_AU_TYPE "aufx"
#define PB_LATENTIE 0
#define PB_N_PARAMS 9
#define PB_PARAMS \
    {1, "Time", "Hoe lang tot de echo komt. Draaien laat de toon glijden, zoals een band die sneller gaat", "ms", 20, 1200, 375, 250, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {2, "Feedback", "Hoeveel van de echo weer de band op gaat. Boven 1 gaat hij vanzelf door (zelfoscillatie)", "", 0, 1.25, 0.5, 30, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {3, "Tone", "Hoe donker elke herhaling wordt: het filter in de lus haalt hoog weg", "Hz", 300, 12000, 3000, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {4, "Low cut", "Haalt laag uit de lus zodat de herhalingen niet dichtslibben", "Hz", 20, 1000, 100, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {5, "Resonance", "Een piek op de toonfrequentie in de lus; hoger = fluitender herhalingen", "Q", 0.5, 4, 0.70703125, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {6, "Saturation", "Hoe vroeg de band dichtloopt: warmer, en het remt de zelfoscillatie", "dB", 0, 24, 6, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {7, "Flutter", "Wow en flutter: hoe onrustig de band loopt. 0,25 is een goede bandrecorder, 1 een versleten band", "", 0, 1, 0.25, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {8, "Mix", "Hoeveel echo je hoort naast het origineel", "", 0, 1, 0.375, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}, \
    {9, "Hiss", "De sis van de band in de echo; bij 1 zoals een echte bandrecorder, en hij stapelt in de herhalingen", "", 0, 1, 0, 20, pb::AUTOMATEERBAAR | pb::GLIJDEN}

static constexpr double PB_KETEN[] = {
  12, 9, 28, -1, 0, -1, 0, 6, 0.25,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 29, -1, 0, -1, 0, 8, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 0, -1, 0, -1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 2, 2, 0, 11, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 4, 3, 0, -1, 0, 5, 6,
  -1, 0, 0, -1, 0.099609375, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 5, 4, 0, 0, 0, 0, 375,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  1200, 0.5, 2, 5, 0, 1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 6, 6, 0, -1, 0, 3, 100,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 7, 7, 0, -1, 0, 2, 3000,
  -1, 0, 0, 4, 0.70703125, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 9, 2, 0, 8, 0, 7, 0.375,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 1, 9, 0, -1, 0, -1, 0,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 3, 8, 0, -1, 0, 1, 0.5,
  -1, 0, 0, -1, 0, -1, 0, 0, -1,
  0, -1, 0, 0, -1, 0, -1, 0, 0,
  0, 0, 20, 1200, 375, 250, 0, 1.25, 0.5,
  30, 300, 12000, 3000, 20, 20, 1000, 100, 20,
  0.5, 4, 0.70703125, 20, 0, 24, 6, 20, 0,
  1, 0.25, 20, 0, 1, 0.375, 20, 0, 1,
  0, 20
};
static constexpr int PB_KETEN_N = 362;

#include "../graaf_ontwerp.h"
