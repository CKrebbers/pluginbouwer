# Dub-echo — de CLAP-schil rond het Ontwerp uit dsp/, en via clap-wrapper v0.16.0 ook VST3 en AUv2.
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host van de null-test).
cmake_minimum_required(VERSION 3.21)
project(dub_echo VERSION 0.1.0 LANGUAGES C CXX)
if (APPLE)
  # valkuil 3: zonder deze twee mist CMAKE_OBJCXX_COMPILE_OBJECT voor de AU-bronnen van clap-wrapper
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # de statische impl gaat in drie gedeelde modules
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

get_filename_component(PB_PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(PB_KANARIE "Bouw een kanarie: malloc in process (RTSan MOET dit vangen). Nooit voor een echte plugin." OFF)
option(PB_RTSAN "Bouw pb-render met -fsanitize=realtime (clang >= 20)" OFF)
set(PB_FORMATEN "CLAP;VST3;AUV2" CACHE STRING "Formaten voor make_clapfirst_plugins")

# clap-wrapper v0.16.0 met de patches uit patches/ (in naamvolgorde; valkuilen 1, 7, 10, 13, 17). Het script slaat een
# patch over die er al op zit, zodat een tweede configure niet faalt.
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 ON CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh ${PB_PAKKET}/gereedschap/pas-patches-toe.sh ${PB_PAKKET}/patches
)
FetchContent_MakeAvailable(clap-wrapper)

# De DSP: gegeven, met vaste rekenregels (CLAUDE.md). Alles wat dsp/ insluit krijgt deze vlaggen.
set(PB_FP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)

# Het frontpaneel (fase 3): het model is overal hetzelfde; tekenen alleen op macOS (Cocoa). Elders geen gui-extensie.
# De lettertypen (OFL, plugin/lettertypen/) gaan als byte-rij in het binary: geen bestanden naast de plugin.
set(PB_LETTERTYPEN_C ${CMAKE_BINARY_DIR}/lettertypen.c)
add_custom_command(OUTPUT ${PB_LETTERTYPEN_C}
  COMMAND ${CMAKE_COMMAND} -DUIT=${PB_LETTERTYPEN_C}
    "-DBRONNEN=plexmono=${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/IBMPlexMono-Regular.ttf;silkscreen=${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/Silkscreen-Regular.ttf"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bytes.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/IBMPlexMono-Regular.ttf
          ${CMAKE_CURRENT_SOURCE_DIR}/lettertypen/Silkscreen-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bytes.cmake
  VERBATIM)

set(PB_PANEEL_BRONNEN src/paneel_model.cpp)
if (APPLE)
  list(APPEND PB_PANEEL_BRONNEN src/paneel_mac.mm ${PB_LETTERTYPEN_C})
else()
  list(APPEND PB_PANEEL_BRONNEN src/paneel_geen.cpp)
endif()

add_library(dub_echo_impl STATIC src/dub_echo.cpp src/tekst.cpp ${PB_PANEEL_BRONNEN})
target_include_directories(dub_echo_impl PUBLIC ${PB_PAKKET}/dsp)
target_compile_options(dub_echo_impl PRIVATE ${PB_FP_VLAGGEN})
target_link_libraries(dub_echo_impl PUBLIC clap)
if (APPLE)
  target_link_libraries(dub_echo_impl PUBLIC "-framework Cocoa" "-framework CoreText" "-framework CoreGraphics")
endif()
if (PB_KANARIE)
  target_compile_definitions(dub_echo_impl PRIVATE PB_KANARIE=1)
endif()

make_clapfirst_plugins(
  TARGET_NAME dub_echo
  IMPL_TARGET dub_echo_impl
  OUTPUT_NAME "PB Dub-echo"
  ENTRY_SOURCE src/dub_echo_entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.dub-echo"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  PLUGIN_FORMATS ${PB_FORMATEN}
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PDca"
  AUV2_INSTRUMENT_TYPE "aufx"
)

# valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen (crash na "SUCCESS")
if (TARGET dub_echo_vst3 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# pb-render: de CLAP-host voor de null-test en RTSan (gereedschap/pb-render.cpp, gegeven)
add_executable(pb-render ${PB_PAKKET}/gereedschap/pb-render.cpp)
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()

# de eigen toets van fase 2 (state-blobs met de hand, tekst ↔ waarde), door het gebouwde .clap
add_executable(pb-statetoets toets/state_toets.cpp)
target_link_libraries(pb-statetoets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-statetoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# de eigen toets van fase 3, deel 1 (elk platform): het paneelmodel tegen de mockup en de brievenbus (ook met twee draden)
add_executable(pb-paneeltoets toets/paneel_toets.cpp src/paneel_model.cpp)
target_include_directories(pb-paneeltoets PRIVATE src ${PB_PAKKET}/dsp)
target_link_libraries(pb-paneeltoets PRIVATE clap)
find_package(Threads REQUIRED)
target_link_libraries(pb-paneeltoets PRIVATE Threads::Threads)
set_target_properties(pb-paneeltoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# deel 2 (macOS): het paneel door het gebouwde .clap openen in een venster, een schermafbeelding (PNG) maken en met de
# muis aan de knoppen draaien; de waarden moeten via de host (flush/process als uitvoerevents) terugkomen
if (APPLE)
  add_executable(pb-guitoets toets/gui_toets.mm)
  target_link_libraries(pb-guitoets PRIVATE clap ${CMAKE_DL_LIBS} "-framework Cocoa")
  set_target_properties(pb-guitoets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
