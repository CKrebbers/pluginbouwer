// entry.cpp — het CLAP-instappunt. clap-wrapper compileert dit bestand per formaat opnieuw (make_clapfirst_plugins).
#include <clap/clap.h>
#include "entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, pb_entry_init, pb_entry_deinit, pb_entry_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
