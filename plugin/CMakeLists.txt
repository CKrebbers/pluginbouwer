# CMake voor de plugin "Dub-echo": een CLAP (de echte plugin) plus VST3 en AUv2 via clap-wrapper v0.16.0.
#
#   cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Uitvoer: build/plugins/PB Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host uit gereedschap/).
# Opties: PB_KANARIE=ON  bouwt een plugin die in process alloceert (de RTSan-kanarie, moet falen)
#         PB_RTSAN=ON    bouwt pb-render met -fsanitize=realtime (clang ≥ 20)
cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS-minimum")
project(dub_echo VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  # valkuil 3: anders ontbreekt CMAKE_OBJCXX_COMPILE_OBJECT voor de AUv2-bronnen van clap-wrapper
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PB_KANARIE "RTSan-kanarie: malloc in process (moet in CI falen)" OFF)
option(PB_RTSAN "pb-render met -fsanitize=realtime bouwen" OFF)

get_filename_component(PB_PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PB_DSP "${PB_PAKKET}/dsp")
set(PB_KETEN_H "${PB_DSP}/ontwerpen/keten_dub_echo.h")

# Naam, versie en AU-codes uit de gegenereerde header, zodat ze nooit uit elkaar lopen.
function(pb_lees_define naam uit)
  file(STRINGS "${PB_KETEN_H}" regel REGEX "^#define ${naam} \"[^\"]*\"")
  string(REGEX REPLACE "^#define ${naam} \"([^\"]*)\".*" "\\1" waarde "${regel}")
  if(waarde STREQUAL "")
    message(FATAL_ERROR "${naam} niet gevonden in ${PB_KETEN_H}")
  endif()
  set(${uit} "${waarde}" PARENT_SCOPE)
endfunction()
pb_lees_define(PB_NAAM PB_NAAM)
pb_lees_define(PB_VERSIE PB_VERSIE)
pb_lees_define(PB_MAKER PB_MAKER)
pb_lees_define(PB_CLAP_ID PB_CLAP_ID)
pb_lees_define(PB_AU_MAKER PB_AU_MAKER)
pb_lees_define(PB_AU_SUBTYPE PB_AU_SUBTYPE)
message(STATUS "Dub-echo: ${PB_NAAM} ${PB_VERSIE}, AU aufx ${PB_AU_SUBTYPE} ${PB_AU_MAKER}")

# --- afhankelijkheden ------------------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(clap
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG 1.2.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(clap)

set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# valkuil 1: AU-parameters vóór Initialize bereiken de CLAP anders niet. git apply faalt hard als de patch niet past.
find_package(Git REQUIRED)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PB_PAKKET}/patches/clap-wrapper-auv2-params-voor-init.patch"
  UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(clap-wrapper)

# --- de DSP-vlaggen (CLAUDE.md: bit voor bit gelijk aan de WASM) -----------------------------------------------
set(PB_DSP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)

# --- de plugin ------------------------------------------------------------------------------------------------
add_library(dub_echo_impl STATIC src/dub_echo.cpp)
target_include_directories(dub_echo_impl PUBLIC "${PB_DSP}" src)
target_link_libraries(dub_echo_impl PUBLIC clap)
target_compile_options(dub_echo_impl PRIVATE ${PB_DSP_VLAGGEN})
if(PB_KANARIE)
  target_compile_definitions(dub_echo_impl PRIVATE PB_KANARIE=1)
endif()

set(PB_FORMATEN CLAP VST3)
if(APPLE)
  list(APPEND PB_FORMATEN AUV2)
endif()

make_clapfirst_plugins(
  TARGET_NAME dub_echo
  IMPL_TARGET dub_echo_impl
  OUTPUT_NAME "${PB_NAAM}"
  ENTRY_SOURCE src/entry.cpp
  BUNDLE_IDENTIFIER "${PB_CLAP_ID}"
  BUNDLE_VERSION "${PB_VERSIE}"
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS ${PB_FORMATEN}
  ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
  AUV2_MANUFACTURER_NAME "${PB_MAKER}"
  AUV2_MANUFACTURER_CODE "${PB_AU_MAKER}"
  AUV2_SUBTYPE_CODE "${PB_AU_SUBTYPE}"
  AUV2_INSTRUMENT_TYPE "aufx"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen (crash na "SUCCESS")
  target_link_options(dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# --- pb-render: de CLAP-host voor de null-test en RTSan ---------------------------------------------------------
add_executable(pb-render "${PB_PAKKET}/gereedschap/pb-render.cpp")
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()
