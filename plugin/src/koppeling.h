// koppeling.h — het paneel en een plugin-instantie (fase 3). plugin.cpp levert de koppeling: lezen uit de atomaire
// spiegel, een wijziging in de lock-vrije rij naar process/params.flush (en request_flush), value_to_text.
// paneel_mac.mm gebruikt hem via de gui-extensie; pb-paneeltoets (Linux en macOS) roept hem rechtstreeks aan op een
// instantie uit de statische impl-bibliotheek, om het model met de echte rij te toetsen.
#pragma once

#include <clap/clap.h>

#include <string>
#include <vector>

#include "paneel.h"

// false als `plugin` geen PB Toetsenpad uit deze bibliotheek is
bool pbPaneelKoppeling(const clap_plugin_t* plugin, pb::paneel::Koppeling* k);
std::vector<pb::paneel::Knop> pbPaneelKnoppen();
std::string pbPaneelNaam();
