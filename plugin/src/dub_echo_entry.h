// dub_echo_entry.h — de drie functies van de CLAP-ingang; dub_echo_entry.cpp maakt er per formaat clap_entry van.
#pragma once
#include <clap/clap.h>

bool dub_echo_entry_init(const char* plugin_path);
void dub_echo_entry_deinit();
const void* dub_echo_entry_get_factory(const char* factory_id);
