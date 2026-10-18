// dub_echo_entry.h — de drie functies van clap_entry, uit de statische bibliotheek (make_clapfirst_plugins).
#pragma once

bool dub_echo_init(const char* plugin_path);
void dub_echo_deinit(void);
const void* dub_echo_get_factory(const char* factory_id);
