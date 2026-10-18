# CMake voor Toetsenpad: een CLAP-plugin (C++17), en via clap-wrapper v0.16.0 ook VST3 en AUv2.
#
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Uitvoer: build/plugins/PB Toetsenpad.{clap,vst3,component}, build/pb-render (de CLAP-host van de null-test),
# build/pb-statetoets (de state- en teksttoets van fase 2), build/pb-paneeltoets (het paneel, fase 3) en op macOS
# build/pb-scherm (het Cocoa-paneel via de gui-extensie, met schermafbeelding).
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

# --- de plugin als statische bibliotheek (make_clapfirst_plugins linkt hem in elk formaat) --------------------------
add_library(toetsenpad-impl STATIC src/toetsenpad.cpp ${PB_PANEEL_BRONNEN})
target_include_directories(toetsenpad-impl PUBLIC ${PAKKET}/dsp src extern)
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
  target_include_directories(toetsenpad-kanarie PRIVATE ${PAKKET}/dsp src extern)
  target_link_libraries(toetsenpad-kanarie PRIVATE clap)
  target_compile_definitions(toetsenpad-kanarie PRIVATE PB_KANARIE=1)
  target_compile_options(toetsenpad-kanarie PRIVATE ${PB_DSP_VLAGGEN})
  set_target_properties(toetsenpad-kanarie PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "kanarie"
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kanarie)
endif()
