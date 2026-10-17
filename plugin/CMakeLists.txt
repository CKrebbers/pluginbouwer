# CMakeLists.txt — de CLAP-schil van Dub-echo, en via clap-wrapper v0.16.0 ook VST3 en AUv2.
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host voor de null-test).
cmake_minimum_required(VERSION 3.21)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "laagste macOS")
project(dub_echo VERSION 0.1.0 LANGUAGES C CXX)
if (APPLE)
  # valkuil 3: clap-wrapper (AUv2, VST3 op macOS) heeft Objective-C++ nodig
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(PB_KANARIE "malloc in process (alleen voor de RTSan-toets: die MOET falen)" OFF)
option(PB_RTSAN "pb-render met -fsanitize=realtime (clang >= 20)" OFF)

# --- clap-wrapper v0.16.0 (MIT), met de patches uit patches/ in naamvolgorde -----------------------------------
include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh "${PAKKET}/gereedschap/pas-patches-toe.sh" "${PAKKET}/patches"
)
FetchContent_MakeAvailable(clap-wrapper)
if (NOT TARGET clap)
  guarantee_clap()  # de CLAP-headers (clap/clap.h) die clap-wrapper ophaalt
endif()

# --- de DSP: gegeven, alleen deze vlaggen (CLAUDE.md) -----------------------------------------------------------
add_library(pb-dsp INTERFACE)
target_include_directories(pb-dsp INTERFACE "${PAKKET}/dsp")
target_compile_options(pb-dsp INTERFACE -ffp-contract=off -fno-fast-math -fno-math-errno)

# --- de plugin als statische bibliotheek (clap-first) ----------------------------------------------------------
add_library(dub_echo_impl STATIC src/dub_echo.cpp)
target_link_libraries(dub_echo_impl PUBLIC clap PRIVATE pb-dsp)
if (PB_KANARIE)
  target_compile_definitions(dub_echo_impl PRIVATE PB_KANARIE=1)
endif()

make_clapfirst_plugins(
  TARGET_NAME keten_dub_echo
  IMPL_TARGET dub_echo_impl
  OUTPUT_NAME "PB Dub-echo"
  ENTRY_SOURCE src/entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.dub-echo"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PDca"
  AUV2_INSTRUMENT_TYPE "aufx"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen
  target_link_options(keten_dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# --- pb-render: de CLAP-host voor de null-test en RTSan ---------------------------------------------------------
add_executable(pb-render "${PAKKET}/gereedschap/pb-render.cpp")
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()
