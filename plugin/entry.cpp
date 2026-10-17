// entry.cpp — de geëxporteerde clap_entry. clap-wrapper compileert dit bestand opnieuw voor elk formaat
// (CLAP, VST3, AUv2) en koppelt het aan dezelfde statische bibliotheek.
#include <clap/clap.h>

#include "entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
const CLAP_EXPORT clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, pb_entry_init, pb_entry_deinit, pb_entry_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
