// paneel_mac.h — de Cocoa-kant van het frontpaneel (paneel_mac.mm). Alleen de main thread roept dit aan.
#pragma once

#include "paneel.h"

namespace pb_ui {

// Maakt de NSView (de aanroeper houdt één referentie vast tot `sluit`). nullptr als het niet lukt.
void* maakView(Bediening* b, double breedte, double hoogte);
bool zetOuder(void* view, void* ouderNSView);
void zetMaat(void* view, double breedte, double hoogte);
void toon(void* view, bool zichtbaar);
// Stopt de timer, haalt de view uit zijn ouder, laat de Bediening los en geeft de referentie terug.
void sluit(void* view);

}  // namespace pb_ui
