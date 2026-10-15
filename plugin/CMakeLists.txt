# Dub-echo — de CLAP-schil rond het Ontwerp uit dsp/, en clap-wrapper v0.16.0 voor VST3 en AUv2.
#
#   cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Uitvoer: build/plugins/PB Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host voor de null-test).
cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "laagste macOS")
project(dub_echo VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  # valkuil 3: clap-wrapper compileert .mm-bestanden; zonder deze talen ontbreekt CMAKE_OBJCXX_COMPILE_OBJECT
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

get_filename_component(PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(DUB_ECHO_KANARIE "RTSan-kanarie: process roept malloc aan (moet door RTSan gevangen worden)" OFF)
option(DUB_ECHO_RTSAN "bouw pb-render met -fsanitize=realtime (clang >= 20)" OFF)

# --- clap-wrapper v0.16.0, met de patches uit patches/ -----------------------------------------------------------
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  # valkuil 1: AU-parameters vóór Initialize
  PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${PAKKET}/patches/clap-wrapper-auv2-params-voor-init.patch
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pas-patch-toe.cmake
  # thread-check zonder thread_local: dyld reserveert die met calloc bij de eerste toegang op de audiothread,
  # en pluginval --rtcheck vangt dat in Render (macOS, gezien in CI)
        COMMAND ${CMAKE_COMMAND} -DPATCH=${PAKKET}/patches/clap-wrapper-threadcheck-zonder-tls.patch
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pas-patch-toe.cmake
)
FetchContent_MakeAvailable(clap-wrapper)

# --- de plugin als statische bibliotheek (clap-first) --------------------------------------------------------------
add_library(dub_echo_impl STATIC src/dub_echo.cpp)
target_include_directories(dub_echo_impl PUBLIC "${PAKKET}/dsp" src)
target_link_libraries(dub_echo_impl PUBLIC clap)
# CLAUDE.md: de DSP precies zoals de WASM rekent
target_compile_options(dub_echo_impl PRIVATE -ffp-contract=off -fno-fast-math -fno-math-errno)
if(DUB_ECHO_KANARIE)
  target_compile_definitions(dub_echo_impl PRIVATE DUB_ECHO_KANARIE=1)
endif()

if(DUB_ECHO_KANARIE)
  set(DUB_ECHO_FORMATEN CLAP)
else()
  set(DUB_ECHO_FORMATEN CLAP VST3 AUV2)
endif()

make_clapfirst_plugins(
  TARGET_NAME dub_echo
  IMPL_TARGET dub_echo_impl
  OUTPUT_NAME "PB Dub-echo"
  ENTRY_SOURCE src/dub_echo_entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.dub-echo"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS ${DUB_ECHO_FORMATEN}
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PDca"
  AUV2_INSTRUMENT_TYPE "aufx"
)

if(UNIX AND NOT APPLE AND TARGET dub_echo_vst3)
  # valkuil 2: statics in de VST3 SDK draaien bij exit in uitgeladen geheugen (crash na "SUCCESS" in pluginval)
  target_link_options(dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# --- pb-render: de CLAP-host voor null-test en RTSan ---------------------------------------------------------------
add_executable(pb-render "${PAKKET}/gereedschap/pb-render.cpp")
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(DUB_ECHO_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()
