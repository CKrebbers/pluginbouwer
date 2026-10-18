// entry.cpp — exporteert clap_entry (één keer per formaat gecompileerd door make_clapfirst_plugins).
#include <clap/clap.h>

#include "entry.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, pbEntryInit, pbEntryDeinit, pbEntryGetFactory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
