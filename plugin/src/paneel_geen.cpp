// paneel_geen.cpp — op een platform zonder paneel (Linux, Windows) geen gui-extensie: de host toont zijn eigen
// knoppen. Het frontpaneel is er voor macOS (paneel_mac.mm).
#include "paneel.h"

namespace dub {
const clap_plugin_gui_t* paneelGui() { return nullptr; }
}  // namespace dub
