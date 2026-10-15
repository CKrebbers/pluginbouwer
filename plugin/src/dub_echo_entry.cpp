// dub_echo_entry.cpp — exporteert clap_entry; clap-wrapper compileert dit bestand opnieuw voor CLAP, VST3 en AUv2.
#include <clap/clap.h>

#include "dub_echo_entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

const CLAP_EXPORT struct clap_plugin_entry clap_entry = {CLAP_VERSION, dub_echo_entry_init, dub_echo_entry_deinit,
                                                         dub_echo_entry_get_factory};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
