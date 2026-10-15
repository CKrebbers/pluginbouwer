// dub_echo_entry.h — de drie entry-functies die de statische bibliotheek levert (clap-first van clap-wrapper).
#pragma once

bool dub_echo_entry_init(const char* plugin_path);
void dub_echo_entry_deinit(void);
const void* dub_echo_entry_get_factory(const char* factory_id);
