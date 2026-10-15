// dub_echo_entry.cpp — exporteert clap_entry. clap-wrapper compileert dit bestand opnieuw voor elk formaat
// (CLAP, VST3, AUv2) en koppelt het aan de statische bibliotheek met de plugin zelf.
#include <clap/clap.h>

#include "dub_echo_entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, dub_echo_entry_init, dub_echo_entry_deinit,
                                                    dub_echo_entry_get_factory};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
