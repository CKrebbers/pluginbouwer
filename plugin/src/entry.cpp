// entry.cpp — exporteert clap_entry; clap-wrapper bouwt dit bestand per formaat (CLAP, VST3, AUv2) opnieuw.
#include <clap/clap.h>

bool dub_echo_entry_init(const char* path);
void dub_echo_entry_deinit();
const void* dub_echo_entry_get_factory(const char* factory_id);

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
