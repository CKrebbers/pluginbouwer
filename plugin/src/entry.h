#pragma once
bool pb_entry_init(const char* plugin_path);
void pb_entry_deinit();
const void* pb_entry_get_factory(const char* factory_id);
