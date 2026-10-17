// entry.h — de drie functies van de CLAP-entry, uit de statische impl-bibliotheek (clap-wrapper "clap first").
#pragma once
#include <clap/clap.h>

bool pb_entry_init(const char* pad);
void pb_entry_deinit();
const void* pb_entry_get_factory(const char* id);
