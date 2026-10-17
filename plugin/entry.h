// entry.h — de drie functies van de CLAP-entry; ze staan in de statische bibliotheek (dub_echo.cpp).
#pragma once

bool pb_entry_init(const char* plugin_path);
void pb_entry_deinit(void);
const void* pb_entry_get_factory(const char* factory_id);
