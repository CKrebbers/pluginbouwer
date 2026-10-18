# PB Toetsenpad — CLAP-first, met clap-wrapper v0.16.0 (gepatcht) voor VST3 en AUv2.
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB Toetsenpad.{clap,vst3,component}, build/pb-render (de CLAP-host van de null-test) en
# build/pb-statetoets (state- en teksttoets, fase 2), build/pb-paneeltoets (het paneelmodel, fase 3) en op macOS
# build/pb-paneel (het paneel door het binary: openen, bedienen, schermafbeeldingen; fase 3).
#   -DPB_RTSAN=ON    pb-render met -fsanitize=realtime (clang ≥ 20)
#   -DPB_KANARIE=ON  process alloceert (de kanarie: RTSan MOET dan falen)
cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # de impl is statisch, maar komt in een gedeelde module
if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()

project(pb-toetsenpad VERSION 0.1.0 LANGUAGES C CXX)
if (APPLE)
  # valkuil 3: anders ontbreekt CMAKE_OBJCXX_COMPILE_OBJECT voor de AUv2-wrapper
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

option(PB_RTSAN "pb-render met -fsanitize=realtime" OFF)
option(PB_KANARIE "process alloceert (moet falen in RTSan)" OFF)

get_filename_component(PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# clap-wrapper v0.16.0 met de patches uit patches/, in naamvolgorde (valkuilen 1, 7, 10, 13, 17). Het script slaat een
# patch over die er al op zit, zodat een tweede configure niet faalt.
include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh ${PAKKET}/gereedschap/pas-patches-toe.sh ${PAKKET}/patches
)
FetchContent_MakeAvailable(clap-wrapper)

# De impl: de schil plus de gegeven DSP (include-pad dsp/). De DSP-vlaggen gelden voor alles wat de DSP compileert.
add_library(toetsenpad-impl STATIC src/plugin.cpp src/paneel.cpp)
target_include_directories(toetsenpad-impl PUBLIC ${PAKKET}/dsp ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(toetsenpad-impl PUBLIC clap)
target_compile_options(toetsenpad-impl PRIVATE -ffp-contract=off -fno-fast-math -fno-math-errno)

# Het frontpaneel (fase 3). Op macOS een NSView (CLAP gui-extensie, Cocoa); clap-wrapper geeft hem door aan VST3 en
# AUv2. De lettertypen (IBM Plex Mono en Silkscreen, SIL OFL 1.1, zie lettertypen/) gaan als bytes in het binary:
# dan hoeft er niets naast de plugin te staan en is er geen installatie van een lettertype nodig.
if (APPLE)
  set(PB_LETTERS ${CMAKE_BINARY_DIR}/lettertypen.cpp)
  set(PB_LETTER_BRON ${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/IBMPlexMono-Regular.ttf
                     ${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/Silkscreen-Regular.ttf)
  string(REPLACE ";" "|" PB_LETTER_BRONNEN "${PB_LETTER_BRON}")  # een ; zou het commando in stukken knippen
  add_custom_command(OUTPUT ${PB_LETTERS}
    COMMAND ${CMAKE_COMMAND} "-DUIT=${PB_LETTERS}" "-DBRONNEN=${PB_LETTER_BRONNEN}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/insluiten.cmake
    DEPENDS ${PB_LETTER_BRON} ${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/insluiten.cmake
    COMMENT "Lettertypen insluiten"
    VERBATIM)
  target_sources(toetsenpad-impl PRIVATE src/paneel_mac.mm ${PB_LETTERS})
  target_link_libraries(toetsenpad-impl PUBLIC "-framework Cocoa" "-framework CoreText" "-framework CoreGraphics")
endif()
if (PB_KANARIE)
  target_compile_definitions(toetsenpad-impl PRIVATE PB_KANARIE=1)
endif()

make_clapfirst_plugins(
  TARGET_NAME toetsenpad
  IMPL_TARGET toetsenpad-impl
  OUTPUT_NAME "PB Toetsenpad"
  ENTRY_SOURCE src/entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.toetsenpad"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PTgg"
  AUV2_INSTRUMENT_TYPE "aumu"
)

if (UNIX AND NOT APPLE)
  # valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen
  target_link_options(toetsenpad_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# pb-render: de kleine CLAP-host van de null-test en de RTSan-toets
add_executable(pb-render ${PAKKET}/gereedschap/pb-render.cpp)
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()

# pb-statetoets: de toets van fase 2 door het gebouwde .clap (state-blobs met de hand, tekst ↔ waarde)
add_executable(pb-statetoets toetsen/pb-statetoets.cpp)
target_link_libraries(pb-statetoets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-statetoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# pb-paneeltoets: het model van het paneel met de echte koppeling (rij, flush, process), uit de statische impl
add_executable(pb-paneeltoets toetsen/pb-paneeltoets.cpp)
target_link_libraries(pb-paneeltoets PRIVATE toetsenpad-impl)
set_target_properties(pb-paneeltoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (APPLE)
  # pb-paneel: een kleine CLAP-host met een venster; opent het paneel van het gebouwde .clap, bedient het met de muis
  # en schrijft schermafbeeldingen (PNG)
  add_executable(pb-paneel toetsen/pb-paneel.mm)
  target_link_libraries(pb-paneel PRIVATE clap ${CMAKE_DL_LIBS} "-framework Cocoa")
  set_target_properties(pb-paneel PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
