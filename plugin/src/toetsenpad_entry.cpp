// toetsenpad_entry.cpp — het geëxporteerde clap_entry. make_clapfirst_plugins compileert dit bestand opnieuw voor elk
// formaat (CLAP, VST3, AUv2) en linkt het met de statische bibliotheek waarin de plugin zelf staat (toetsenpad.cpp).
#include <clap/clap.h>

#include "toetsenpad_entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
const CLAP_EXPORT clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, toetsenpad_entry_init, toetsenpad_entry_deinit,
                                                    toetsenpad_entry_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
