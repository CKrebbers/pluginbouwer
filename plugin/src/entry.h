// entry.h — de entry-functies uit de statische impl-bibliotheek; entry.cpp exporteert ze als clap_entry.
#pragma once

bool pb_entry_init(const char* pad);
void pb_entry_deinit();
const void* pb_entry_get_factory(const char* id);
