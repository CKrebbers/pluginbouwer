// paneel_mac.h — het paneel als NSView (paneel_mac.mm), voor de CLAP gui-extensie in plugin.cpp (alleen macOS).
// Alles op de mainthread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "paneel.h"

struct PbVenster;
PbVenster* pbVensterMaak(const pb::paneel::Koppeling& k, std::vector<pb::paneel::Knop> knoppen, std::string naam,
                         uint32_t breed, uint32_t hoog);
void pbVensterWeg(PbVenster* v);                 // sluit een open gebaar, haalt de view weg
bool pbVensterOuder(PbVenster* v, void* nsview); // als subview van de NSView van de host
void pbVensterMaat(PbVenster* v, uint32_t breed, uint32_t hoog);
void pbVensterToon(PbVenster* v, bool zichtbaar);
