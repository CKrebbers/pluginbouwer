// entry.cpp — exporteert clap_entry (make_clapfirst_plugins bouwt dit bestand in de CLAP, VST3 en AUv2).
#include <clap/clap.h>

#include "entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, pb_entry_init, pb_entry_deinit,
                                                    pb_entry_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
