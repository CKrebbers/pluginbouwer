# Toetsenpad — de CLAP-plugin, en via clap-wrapper v0.16.0 de VST3 en AUv2 (make_clapfirst_plugins).
#   cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB Toetsenpad.{clap,vst3,component} en build/pb-render (gereedschap/pb-render.cpp).
cmake_minimum_required(VERSION 3.21)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "laagste macOS")
project(toetsenpad VERSION 0.1.0 LANGUAGES C CXX)
if (APPLE)
  # valkuil 3: clap-wrapper bouwt Objective-C(++) voor de AU
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(PB_RTSAN "pb-render bouwen met -fsanitize=realtime (clang >= 20)" OFF)
option(PB_KANARIE "ook toetsenpad_kanarie bouwen: een .clap met een malloc in process, die RTSan moet vangen" OFF)

# --- clap-wrapper v0.16.0, met de patches uit patches/ in naamvolgorde (CLAUDE.md valkuilen 1, 7, 10, 13, 17) -----
include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_OUTPUT_NAME "toetsenpad-clap-wrapper" CACHE STRING "" FORCE)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh "${PAKKET}/gereedschap/pas-patches-toe.sh" "${PAKKET}/patches"
)
FetchContent_MakeAvailable(clap-wrapper)

# --- de DSP: gegeven, met de vlaggen die hem bit voor bit gelijk aan de WASM houden ------------------------------
set(PB_DSP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)

function(pb_impl naam)
  add_library(${naam} STATIC src/plugin.cpp)
  target_include_directories(${naam} PUBLIC "${PAKKET}/dsp" "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_options(${naam} PRIVATE ${PB_DSP_VLAGGEN})
  target_link_libraries(${naam} PUBLIC clap)
endfunction()

pb_impl(toetsenpad-impl)

make_clapfirst_plugins(
  TARGET_NAME toetsenpad
  IMPL_TARGET toetsenpad-impl
  OUTPUT_NAME "PB Toetsenpad"
  ENTRY_SOURCE src/entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.toetsenpad"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PTgg"
  AUV2_INSTRUMENT_TYPE "aumu"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen
  target_link_options(toetsenpad_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# --- pb-render: de kleine CLAP-host van de null-test (en van RTSan) ----------------------------------------------
add_executable(pb-render "${PAKKET}/gereedschap/pb-render.cpp")
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()

# --- de RTSan-kanarie: dezelfde plugin met een malloc in process (valkuil 4); RTSan MOET hem vangen --------------
if (PB_KANARIE)
  pb_impl(toetsenpad-kanarie-impl)
  target_compile_definitions(toetsenpad-kanarie-impl PRIVATE PB_KANARIE=1)
  add_library(toetsenpad_kanarie MODULE src/entry.cpp)
  target_link_libraries(toetsenpad_kanarie PRIVATE toetsenpad-kanarie-impl)
  set_target_properties(toetsenpad_kanarie PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "kanarie"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/kanarie")
endif()
