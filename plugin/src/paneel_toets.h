// paneel_toets.h — een eigen CLAP-extensie om het paneel zonder venster te toetsen (plugin/toets/paneeltoets.cpp).
//
// Een host kent hem niet en vraagt hem dus niet op; hij verandert niets aan de plugin. De toets maakt er een paneel mee,
// tekent het in een buffer (de schermafbeelding op Linux) en stuurt er muisacties naar, zodat het hele pad door het
// gebouwde binary loopt: muis → gebaar → lock-vrije rij → request_flush → flush/process → events naar de host.
// Alle functies: mainthread.
#pragma once

#include <clap/clap.h>
#include <stdint.h>

#define PB_EXT_PANEELTOETS "nl.varve.pluginbouwer.paneeltoets/1"

typedef struct pb_paneeltoets {
  // een paneel van b × h punten zonder venster (bestaat er al een, bv. van gui.create, dan wordt dat gebruikt)
  bool (*maak)(const clap_plugin_t* plugin, double b, double h);
  void (*weg)(const clap_plugin_t* plugin);
  // het paneel in rgba (bw × bh pixels, 4 bytes per pixel), schaal = pixels per punt
  bool (*teken)(const clap_plugin_t* plugin, uint8_t* rgba, uint32_t bw, uint32_t bh, double schaal);
  // muisactie (0 neer, 1 slepen, 2 op, 3 dubbelklik) op (x, y) in punten, y omlaag
  bool (*muis)(const clap_plugin_t* plugin, int soort, double x, double y, bool shift);
  // midden van knop k in punten; false als k niet bestaat
  bool (*knop)(const clap_plugin_t* plugin, int k, double* x, double* y);
  // de gekozen knop (laatst aangeraakt), −1 als er geen is
  int (*gekozen)(const clap_plugin_t* plugin);
} pb_paneeltoets_t;
