// entry.h — de drie functies achter clap_entry. Ze staan in de statische impl-bibliotheek; entry.cpp exporteert ze
// (zo wil make_clapfirst_plugins het: dezelfde impl in de .clap, de VST3 en de AU).
#pragma once

bool pb_entry_init(const char* plugin_path);
void pb_entry_deinit(void);
const void* pb_entry_get_factory(const char* factory_id);
