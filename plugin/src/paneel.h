// paneel.h — de CLAP gui-extensie (het frontpaneel uit design.md). clap-wrapper geeft hem door aan VST3 en AUv2.
//
// Wat het paneel van de plugin mag (valkuil 11 en 14): de atomaire kopie van de waarden lezen, een gebaar in de
// brievenbus zetten en de host om een flush vragen. Het Ontwerp en de audiothread raakt het niet.
#pragma once
#include <clap/clap.h>

#include <atomic>

#include "paneel_model.h"

namespace dub {

struct PaneelPlek {
  const std::atomic<double>* kopie = nullptr;  // N_KNOPPEN waarden, elke draad
  Bus* bus = nullptr;
  const clap_host_t* host = nullptr;
  const clap_host_params_t* hostParams = nullptr;  // uit init
  // van het paneel (mainthread)
  void* paneel = nullptr;
  uint32_t breedte = BEGIN_B, hoogte = BEGIN_H;
};

PaneelPlek* paneelPlek(const clap_plugin_t* plugin);  // dub_echo.cpp

// de gui-extensie, of nullptr op een platform zonder paneel (alleen macOS/Cocoa heeft er een)
const clap_plugin_gui_t* paneelGui();

}  // namespace dub
