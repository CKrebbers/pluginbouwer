// dub_echo_entry.cpp — exporteert clap_entry; clap-wrapper compileert dit bestand opnieuw voor CLAP, VST3 en AUv2.
#include <clap/clap.h>

#include "dub_echo_entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, dub_echo_init, dub_echo_deinit,
                                                    dub_echo_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
