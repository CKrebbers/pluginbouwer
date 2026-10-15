# Dub-echo — CLAP-first plugin; clap-wrapper maakt er VST3 en AUv2 van.
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host voor de null-test).
cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()

project(dub_echo VERSION 0.1.0 LANGUAGES C CXX)

# valkuil 3: zonder deze twee ontbreekt CMAKE_OBJCXX_COMPILE_OBJECT voor de AUv2-wrapper
if(APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PB_PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(PB_PLUGINS "${CMAKE_BINARY_DIR}/plugins")

option(PB_RTSAN "Bouw pb-render met -fsanitize=realtime (clang >= 20)" OFF)
option(PB_KANARIE "Bouw ook een kanarie-CLAP met een malloc in process (voor de RTSan-toets)" OFF)

# --- clap-wrapper v0.16.0, met de patch voor AU-parameters vóór Initialize (valkuil 1) ----------------------
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_OUTPUT_NAME "PB Dub-echo" CACHE STRING "" FORCE)
include(FetchContent)
# Patch 2 (eigen, zie VERSLAG.md): de thread-override-tellers van clap-wrapper waren thread_local; op macOS
# alloceert dyld die bij de eerste aanraking per thread, dus midden in de render-callback (pluginval --rtcheck).
set(PB_PATCH1 "${PB_PAKKET}/patches/clap-wrapper-auv2-params-voor-init.patch")
set(PB_PATCH2 "${PB_PAKKET}/patches/clap-wrapper-thread-override-zonder-tlv.patch")
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  # idempotent: bij een tweede configure staan de patches er al in
  PATCH_COMMAND sh -c "git apply --reverse --check '${PB_PATCH1}' 2>/dev/null || git apply '${PB_PATCH1}'"
        COMMAND sh -c "git apply --reverse --check '${PB_PATCH2}' 2>/dev/null || git apply '${PB_PATCH2}'"
)
FetchContent_MakeAvailable(clap-wrapper)

# --- de plugin als statische bibliotheek (clap-first) -------------------------------------------------------
# De DSP-vlaggen (CLAUDE.md): geen samentrekking tot FMA, geen fast-math, geen errno. Zo rekent de plugin bit voor
# bit als de WASM van de tool.
set(PB_DSP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)

function(pb_impl naam)
  add_library(${naam} STATIC src/dub_echo.cpp)
  target_include_directories(${naam} PUBLIC src "${PB_PAKKET}/dsp")
  target_link_libraries(${naam} PUBLIC clap clap-wrapper-extensions)
  target_compile_options(${naam} PRIVATE ${PB_DSP_VLAGGEN})
endfunction()

pb_impl(dub_echo_impl)

make_clapfirst_plugins(
  TARGET_NAME dub_echo
  IMPL_TARGET dub_echo_impl
  OUTPUT_NAME "PB Dub-echo"
  ENTRY_SOURCE src/dub_echo_entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.dub-echo"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY "${PB_PLUGINS}"
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PDca"
  AUV2_INSTRUMENT_TYPE "aufx"
)

# valkuil 2: statics van de VST3 SDK draaien bij exit in uitgeladen geheugen; dan crasht pluginval na "SUCCESS"
if(UNIX AND NOT APPLE)
  target_link_options(dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# --- pb-render: de CLAP-host voor null-test en RTSan --------------------------------------------------------
add_executable(pb-render "${PB_PAKKET}/gereedschap/pb-render.cpp")
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()

# --- kanarie: dezelfde plugin met een malloc in process; RTSan MOET hem vangen ------------------------------
if(PB_KANARIE)
  pb_impl(dub_echo_kanarie_impl)
  target_compile_definitions(dub_echo_kanarie_impl PRIVATE PB_KANARIE=1)
  add_library(dub_echo_kanarie MODULE src/dub_echo_entry.cpp)
  target_link_libraries(dub_echo_kanarie PRIVATE dub_echo_kanarie_impl)
  set_target_properties(dub_echo_kanarie PROPERTIES
    PREFIX "" SUFFIX ".clap" OUTPUT_NAME "PB Dub-echo kanarie"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/kanarie")
endif()
