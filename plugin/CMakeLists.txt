# CMake voor Toetsenpad: een CLAP-plugin (C++17), en via clap-wrapper v0.16.0 ook VST3 en AUv2.
#
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Uitvoer: build/plugins/PB Toetsenpad.{clap,vst3,component}, build/pb-render (de CLAP-host van de null-test),
# build/pb-statetoets (de state- en teksttoets van fase 2), build/pb-paneeltoets (het paneel, fase 3),
# build/pb-presettoets (de fabriekspresets, fase 4), build/pb-formaattoets (mono/stereo, samplerates, bypass; fase 5) en op macOS build/pb-scherm (het Cocoa-paneel via de gui-extensie,
# met schermafbeelding).
cmake_minimum_required(VERSION 3.21)
project(toetsenpad VERSION 0.1.0 LANGUAGES C CXX)

# valkuil 3: op macOS moet het project zelf OBJC en OBJCXX aanzetten (de AUv2-wrapper heeft ze nodig)
if (APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# --- clap-wrapper v0.16.0, met de patches uit patches/ (in naamvolgorde) ---------------------------------------------
# De CLAP-headers komen mee (CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES haalt CLAP 1.2.x op, doel `clap`; valkuil 12).
# pas-patches-toe.sh slaat een patch over die er al op zit, zodat een tweede configure niet faalt (valkuil 7).
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh ${PAKKET}/gereedschap/pas-patches-toe.sh ${PAKKET}/patches
)
FetchContent_MakeAvailable(clap-wrapper)

# --- het frontpaneel (fase 3): lettertypen in het binary bakken -----------------------------------------------------
# IBM Plex Mono en Silkscreen (SIL OFL 1.1, plugin/extern/fonts) worden een C-tabel in paneel_fonts.cpp; zo hoeft er
# niets naast de plugin te staan. Alleen bij een veranderd lettertype wordt het bestand opnieuw geschreven.
set(PB_FONTS_CPP ${CMAKE_BINARY_DIR}/gegenereerd/paneel_fonts.cpp)
set(PB_FONTS_INHOUD "// gegenereerd door plugin/CMakeLists.txt uit plugin/extern/fonts (SIL Open Font License 1.1)\n")
foreach(paar "pb_font_plexmono=IBMPlexMono-Regular.ttf" "pb_font_silkscreen=Silkscreen-Regular.ttf")
  string(REPLACE "=" ";" paar "${paar}")
  list(GET paar 0 symbool)
  list(GET paar 1 bestand)
  set(pad ${CMAKE_CURRENT_SOURCE_DIR}/extern/fonts/${bestand})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${pad})
  file(READ ${pad} hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
  string(APPEND PB_FONTS_INHOUD "extern const unsigned char ${symbool}[];\nconst unsigned char ${symbool}[] = {${hex}0};\n")
endforeach()
file(WRITE ${PB_FONTS_CPP}.nieuw "${PB_FONTS_INHOUD}")
configure_file(${PB_FONTS_CPP}.nieuw ${PB_FONTS_CPP} COPYONLY)
set(PB_PANEEL_BRONNEN src/paneel.cpp ${PB_FONTS_CPP})

# --- fabriekspresets (fase 4): presets/<sleutel>.json wordt een tabel in het binary ----------------------------------
# ARCHITECTUUR.md "Presets": { naam, uitleg, waarden: { <parameternaam>: waarde } }. Hier wordt de vorm getoetst (een
# fout is een bouwfout); of elke naam een parameter is en elke waarde binnen het bereik ligt, toetst toetsenpad.cpp met
# static_assert tegen PARAMS uit dsp/ (ook een bouwfout). De volgorde is die van de bestandsnamen.
function(pb_c_tekst uit tekst)
  string(REPLACE "\\" "\\\\" tekst "${tekst}")
  string(REPLACE "\"" "\\\"" tekst "${tekst}")
  string(REPLACE "\n" "\\n" tekst "${tekst}")
  set(${uit} "\"${tekst}\"" PARENT_SCOPE)
endfunction()
file(GLOB PB_PRESET_BESTANDEN CONFIGURE_DEPENDS ${PAKKET}/presets/*.json)
list(SORT PB_PRESET_BESTANDEN)
if (NOT PB_PRESET_BESTANDEN)
  message(FATAL_ERROR "presets/: geen fabriekspresets gevonden (presets/<sleutel>.json)")
endif()
set(PB_PRESETS_H ${CMAKE_BINARY_DIR}/gegenereerd/presets_tabel.h)
set(PB_PRESETS_INHOUD "// gegenereerd door plugin/CMakeLists.txt uit presets/*.json; niet met de hand wijzigen\n#pragma once\n\nnamespace pb_presets {\n\nstruct Waarde {\n  const char* naam;\n  double waarde;\n};\n\nstruct Preset {\n  const char* sleutel;  // de bestandsnaam zonder .json; ook de load key\n  const char* naam;\n  const char* uitleg;\n  const Waarde* waarden;\n  int aantal;\n};\n\n")
set(PB_PRESETS_RIJ "")
set(PB_PRESETS_SLEUTELS "")
set(PB_PRESET_NAMEN "")
set(PB_PRESET_AANTAL 0)
foreach(bestand ${PB_PRESET_BESTANDEN})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${bestand})
  get_filename_component(sleutel ${bestand} NAME_WE)
  if (NOT sleutel MATCHES "^[a-z0-9][a-z0-9-]*$")
    message(FATAL_ERROR "presets/${sleutel}.json: de sleutel (bestandsnaam) mag alleen a-z, 0-9 en - bevatten")
  endif()
  file(READ ${bestand} json)
  string(JSON soort ERROR_VARIABLE fout TYPE "${json}")
  if (fout OR NOT soort STREQUAL "OBJECT")
    message(FATAL_ERROR "presets/${sleutel}.json: geen JSON-object (${fout})")
  endif()
  foreach(veld naam uitleg)
    string(JSON soort ERROR_VARIABLE fout TYPE "${json}" ${veld})
    if (fout OR NOT soort STREQUAL "STRING")
      message(FATAL_ERROR "presets/${sleutel}.json: \"${veld}\" ontbreekt of is geen tekst")
    endif()
    string(JSON ${veld} GET "${json}" ${veld})
  endforeach()
  if (naam STREQUAL "" OR naam MATCHES "[;\n]")
    message(FATAL_ERROR "presets/${sleutel}.json: \"naam\" is leeg of bevat ; of een regeleinde")
  endif()
  if (naam IN_LIST PB_PRESET_NAMEN)
    message(FATAL_ERROR "presets/${sleutel}.json: de naam \"${naam}\" bestaat al")
  endif()
  list(APPEND PB_PRESET_NAMEN "${naam}")
  string(JSON soort ERROR_VARIABLE fout TYPE "${json}" waarden)
  if (fout OR NOT soort STREQUAL "OBJECT")
    message(FATAL_ERROR "presets/${sleutel}.json: \"waarden\" ontbreekt of is geen object")
  endif()
  string(JSON n LENGTH "${json}" waarden)
  string(MAKE_C_IDENTIFIER "w_${sleutel}" w)
  set(waarden "")
  if (n GREATER 0)
    math(EXPR laatste "${n} - 1")
    foreach(k RANGE ${laatste})
      string(JSON pnaam MEMBER "${json}" waarden ${k})
      string(JSON soort TYPE "${json}" waarden "${pnaam}")
      if (NOT soort STREQUAL "NUMBER")
        message(FATAL_ERROR "presets/${sleutel}.json: de waarde van \"${pnaam}\" is geen getal")
      endif()
      string(JSON v GET "${json}" waarden "${pnaam}")
      pb_c_tekst(pnaamC "${pnaam}")
      string(APPEND waarden "    {${pnaamC}, ${v}},\n")
    endforeach()
    string(APPEND PB_PRESETS_INHOUD "constexpr Waarde ${w}[] = {\n${waarden}};\n")
    set(wPtr ${w})
  else()
    set(wPtr nullptr)
  endif()
  pb_c_tekst(sleutelC "${sleutel}")
  pb_c_tekst(naamC "${naam}")
  pb_c_tekst(uitlegC "${uitleg}")
  string(APPEND PB_PRESETS_RIJ "    {${sleutelC}, ${naamC}, ${uitlegC}, ${wPtr}, ${n}},\n")
  string(APPEND PB_PRESETS_SLEUTELS " X(${sleutelC})")
  math(EXPR PB_PRESET_AANTAL "${PB_PRESET_AANTAL} + 1")
endforeach()
string(APPEND PB_PRESETS_INHOUD "\nconstexpr int AANTAL = ${PB_PRESET_AANTAL};\nconstexpr Preset PRESETS[AANTAL] = {\n${PB_PRESETS_RIJ}};\n\n}  // namespace pb_presets\n")
# de sleutels als lijst voor de preprocessor (zo maakt toetsenpad.cpp de provider-id's als constante tekst)
string(APPEND PB_PRESETS_INHOUD "\n#define PB_PRESET_SLEUTELS(X)${PB_PRESETS_SLEUTELS}\n")
file(WRITE ${PB_PRESETS_H}.nieuw "${PB_PRESETS_INHOUD}")
configure_file(${PB_PRESETS_H}.nieuw ${PB_PRESETS_H} COPYONLY)
message(STATUS "Fabriekspresets: ${PB_PRESET_AANTAL} (${PB_PRESET_NAMEN})")

# --- de plugin als statische bibliotheek (make_clapfirst_plugins linkt hem in elk formaat) --------------------------
add_library(toetsenpad-impl STATIC src/toetsenpad.cpp ${PB_PANEEL_BRONNEN})
target_include_directories(toetsenpad-impl PUBLIC ${PAKKET}/dsp src extern ${CMAKE_BINARY_DIR}/gegenereerd)
target_link_libraries(toetsenpad-impl PUBLIC clap)
if (APPLE)
  # de CLAP gui-extensie (Cocoa); clap-wrapper geeft hem door aan VST3 en AUv2
  target_sources(toetsenpad-impl PRIVATE src/paneel_mac.mm)
  set_source_files_properties(src/paneel_mac.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
  target_link_libraries(toetsenpad-impl PUBLIC "-framework Cocoa")
endif()
# De DSP rekent bit voor bit als de WASM van de tool: geen FMA-samentrekking, geen fast-math (CLAUDE.md).
set(PB_DSP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)
target_compile_options(toetsenpad-impl PRIVATE ${PB_DSP_VLAGGEN})

set(PB_PLUGIN_MAP ${CMAKE_BINARY_DIR}/plugins)
make_clapfirst_plugins(
  TARGET_NAME toetsenpad
  IMPL_TARGET toetsenpad-impl
  OUTPUT_NAME "PB Toetsenpad"
  ENTRY_SOURCE src/toetsenpad_entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.toetsenpad"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY ${PB_PLUGIN_MAP}
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PTgg"
  AUV2_INSTRUMENT_TYPE "aumu"
)

# valkuil 2: statics in de VST3 SDK draaien op Linux bij exit in uitgeladen geheugen; nooit uitladen
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(toetsenpad_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# --- pb-render: de CLAP-host van de null-test en de RTSan-toets -------------------------------------------------------
add_executable(pb-render ${PAKKET}/gereedschap/pb-render.cpp)
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- pb-statetoets: de eigen toets van fase 2 (state-blobs, tekst ↔ waarde, flush), door het gebouwde .clap -------------
add_executable(pb-statetoets toets/statetoets.cpp)
target_link_libraries(pb-statetoets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-statetoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- pb-presettoets: de toets van fase 4 (preset-discovery en preset-load), door het gebouwde .clap ---------------------
add_executable(pb-presettoets toets/presettoets.cpp)
target_include_directories(pb-presettoets PRIVATE ${CMAKE_BINARY_DIR}/gegenereerd)
target_link_libraries(pb-presettoets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-presettoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- pb-formaattoets: de toets van fase 5 (kanaalindelingen, samplerates × blokgroottes, bypass), door het .clap ------
add_executable(pb-formaattoets toets/formaattoets.cpp)
target_link_libraries(pb-formaattoets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-formaattoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- pb-paneeltoets: de toets van fase 3 (het paneel zonder venster, door het gebouwde .clap; schermafbeelding) -------
add_executable(pb-paneeltoets toets/paneeltoets.cpp src/paneel.cpp ${PB_FONTS_CPP})
target_include_directories(pb-paneeltoets PRIVATE src extern)
target_link_libraries(pb-paneeltoets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-paneeltoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- pb-scherm (macOS): opent het echte Cocoa-paneel via de CLAP gui-extensie, maakt er een schermafbeelding van en
# sleept met echte NSEvents aan een knop -------------------------------------------------------------------------------
if (APPLE)
  add_executable(pb-scherm toets/scherm_mac.mm src/paneel.cpp ${PB_FONTS_CPP})
  target_include_directories(pb-scherm PRIVATE src extern)
  target_link_libraries(pb-scherm PRIVATE clap "-framework Cocoa")
  set_source_files_properties(toets/scherm_mac.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
  set_target_properties(pb-scherm PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

option(PB_RTSAN "pb-render bouwen met -fsanitize=realtime (clang >= 20)" OFF)
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()

# --- kanarie (alleen voor de RTSan-job): dezelfde plugin met een malloc in process; RTSan MOET hem vangen -------------
option(PB_KANARIE "ook de kanarie-plugin bouwen (plugins/kanarie.clap)" OFF)
if (PB_KANARIE)
  add_library(toetsenpad-kanarie MODULE src/toetsenpad.cpp src/toetsenpad_entry.cpp ${PB_PANEEL_BRONNEN})
  target_include_directories(toetsenpad-kanarie PRIVATE ${PAKKET}/dsp src extern ${CMAKE_BINARY_DIR}/gegenereerd)
  target_link_libraries(toetsenpad-kanarie PRIVATE clap)
  target_compile_definitions(toetsenpad-kanarie PRIVATE PB_KANARIE=1)
  target_compile_options(toetsenpad-kanarie PRIVATE ${PB_DSP_VLAGGEN})
  set_target_properties(toetsenpad-kanarie PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "kanarie"
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kanarie)
endif()
