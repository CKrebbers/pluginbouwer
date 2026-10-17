# CMake voor de plugin "Dub-echo": een CLAP (statische impl + entry), en via clap-wrapper v0.16.0 een VST3 en AUv2.
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host voor null-test en RTSan).
cmake_minimum_required(VERSION 3.21)
project(keten_dub_echo VERSION 0.1.0 LANGUAGES C CXX)

# valkuil 3: clap-wrapper heeft op macOS OBJC/OBJCXX nodig in het project zelf
if (APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(PB_PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(PB_KANARIE "Bouw de RTSan-kanarie: process doet een malloc (moet door RTSan gevangen worden)" OFF)
set(PB_FORMATS "CLAP;VST3;AUV2" CACHE STRING "Welke formaten clap-wrapper maakt")

# clap-wrapper v0.16.0, met de patches uit patches/ in naamvolgorde (valkuilen 1 en 7)
include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh ${PB_PAKKET}/gereedschap/pas-patches-toe.sh ${PB_PAKKET}/patches
)
FetchContent_MakeAvailable(clap-wrapper)

# De DSP (gegeven): alleen headers, met de vlaggen die bit voor bit gelijk aan de WASM houden
add_library(pb-dsp INTERFACE)
target_include_directories(pb-dsp INTERFACE ${PB_PAKKET}/dsp)
target_compile_options(pb-dsp INTERFACE -ffp-contract=off -fno-fast-math -fno-math-errno)

add_library(keten_dub_echo-impl STATIC bron/plugin.cpp)
target_link_libraries(keten_dub_echo-impl PUBLIC clap clap-wrapper-extensions PRIVATE pb-dsp)
if (PB_KANARIE)
  target_compile_definitions(keten_dub_echo-impl PRIVATE PB_KANARIE=1)
endif()

make_clapfirst_plugins(
  TARGET_NAME keten_dub_echo
  IMPL_TARGET keten_dub_echo-impl
  OUTPUT_NAME "PB Dub-echo"
  ENTRY_SOURCE bron/entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.dub-echo"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS ${PB_FORMATS}
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PDca"
  AUV2_INSTRUMENT_TYPE "aufx"
)

# valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen
if (UNIX AND NOT APPLE AND TARGET keten_dub_echo_vst3)
  target_link_options(keten_dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# pb-render: de CLAP-host voor de null-test en RTSan
option(PB_RTSAN "Bouw pb-render met -fsanitize=realtime (clang >= 20)" OFF)
add_executable(pb-render ${PB_PAKKET}/gereedschap/pb-render.cpp)
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()
