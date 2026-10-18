// entry.h — de drie functies van de CLAP-entry, uit de statische impl-bibliotheek (plugin.cpp). entry.cpp exporteert
// ze als clap_entry in elk formaat (CLAP, VST3, AUv2) dat make_clapfirst_plugins maakt.
#pragma once

bool pbEntryInit(const char* pluginPath);
void pbEntryDeinit();
const void* pbEntryGetFactory(const char* factoryId);
