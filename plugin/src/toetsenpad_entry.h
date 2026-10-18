// toetsenpad_entry.h — de drie functies van clap_entry, uit de statische bibliotheek (toetsenpad.cpp).
#pragma once
#include <clap/clap.h>

bool toetsenpad_entry_init(const char* plugin_path);
void toetsenpad_entry_deinit();
const void* toetsenpad_entry_get_factory(const char* factory_id);
