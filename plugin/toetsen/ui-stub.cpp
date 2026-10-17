// ui-stub.cpp — alleen voor de toets: een "view" zonder Cocoa, zodat ui-toets de plugin-kant van het paneel (de
// Bediening: wachtrij, gebaren, waarden naar de host) op elk platform en onder RTSan kan toetsen. De "view" is de
// Bediening zelf; set_parent schrijft hem in het venster dat de toets meegeeft (window.ptr wijst naar een void*).
#include "../ui/paneel_mac.h"

namespace pb_ui {

void* maakView(Bediening* b, double, double) { return b; }
bool zetOuder(void* view, void* ouder) {
  if (!ouder) return false;
  *static_cast<void**>(ouder) = view;
  return true;
}
void zetMaat(void*, double, double) {}
void toon(void*, bool) {}
void sluit(void*) {}

}  // namespace pb_ui
