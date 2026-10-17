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
option(PB_UI_KANARIE "het paneel zet de DSP zelf (alleen voor de TSan-toets van het paneel: die MOET falen)" OFF)

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

# --- lettertypen voor het frontpaneel (SIL OFL 1.1, plugin/fonts/), als bytes in de plugin -------------------------
set(PB_FONTS_CPP "${CMAKE_BINARY_DIR}/fonts_data.cpp")
set(PB_FONTS_INHOUD "// gemaakt door plugin/CMakeLists.txt uit plugin/fonts/; niet bewerken\n")
foreach(paar "pb_font_plexmono=IBMPlexMono-Regular.ttf" "pb_font_silkscreen=Silkscreen-Regular.ttf")
  string(REPLACE "=" ";" paar "${paar}")
  list(GET paar 0 naam)
  list(GET paar 1 bestand)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fonts/${bestand}")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/fonts/${bestand}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
  string(APPEND PB_FONTS_INHOUD "extern const unsigned char ${naam}[];\nconst unsigned char ${naam}[] = {${hex}};\n")
endforeach()
file(CONFIGURE OUTPUT "${PB_FONTS_CPP}" CONTENT "${PB_FONTS_INHOUD}" @ONLY)

# --- het frontpaneel (fase 3): platformvrij paneel + venster (Cocoa op macOS, X11 op Linux) -----------------------
add_library(pb-paneel STATIC src/paneel.cpp src/tekenaar.cpp "${PB_FONTS_CPP}")
target_include_directories(pb-paneel PUBLIC src extern)
target_link_libraries(pb-paneel PUBLIC clap pb-dsp)
if (APPLE)
  target_sources(pb-paneel PRIVATE src/paneel_mac.mm)
  target_link_libraries(pb-paneel PUBLIC "-framework Cocoa" "-framework CoreGraphics")
else()
  find_package(X11 REQUIRED)
  find_package(Threads REQUIRED)
  target_sources(pb-paneel PRIVATE src/paneel_x11.cpp)
  target_link_libraries(pb-paneel PUBLIC X11::X11)
endif()

# --- de plugin als statische bibliotheek (clap-first) ----------------------------------------------------------
add_library(dub_echo_impl STATIC src/dub_echo.cpp)
target_link_libraries(dub_echo_impl PUBLIC clap pb-paneel PRIVATE pb-dsp)
if (PB_KANARIE)
  target_compile_definitions(dub_echo_impl PRIVATE PB_KANARIE=1)
endif()
if (PB_UI_KANARIE)
  target_compile_definitions(dub_echo_impl PRIVATE PB_UI_KANARIE=1)
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

# --- pb-paneel: opent het frontpaneel als host, bedient het met de muis, maakt schermafbeeldingen (fase 3) ---------
add_executable(pb-paneel-toets "${PAKKET}/gereedschap/pb-paneel.cpp")
target_link_libraries(pb-paneel-toets PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-paneel-toets PROPERTIES OUTPUT_NAME pb-paneel RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if (APPLE)
  set_source_files_properties("${PAKKET}/gereedschap/pb-paneel.cpp" PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(pb-paneel-toets PRIVATE "-framework Cocoa")
else()
  target_link_libraries(pb-paneel-toets PRIVATE X11::X11 Threads::Threads)
endif()
if (PB_RTSAN)
  target_compile_options(pb-paneel-toets PRIVATE -fsanitize=realtime)
  target_link_options(pb-paneel-toets PRIVATE -fsanitize=realtime)
endif()

# --- state-toets (fase 2): bouwen zonder één parameter, zoals een oudere versie ----------------------------------
# build/toets/zonder-<id>.clap kent parameter <id> niet; gereedschap/pb-state-toets laadt zijn state in de volledige
# bouw (vooruit-compatibel) en omgekeerd. Alleen voor de toets; niet installeren.
option(PB_TOETSEN "bouw de state-toets en de plugins zonder één parameter" ON)
if (PB_TOETSEN)
  foreach(n RANGE 1 8)
    add_library(dub_echo_zonder_${n} MODULE src/dub_echo.cpp src/entry.cpp)
    target_link_libraries(dub_echo_zonder_${n} PRIVATE clap pb-dsp)
    target_compile_definitions(dub_echo_zonder_${n} PRIVATE PB_ZONDER_PARAM=${n} PB_GEEN_GUI=1)
    set_target_properties(dub_echo_zonder_${n} PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "zonder-${n}"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/toets" CXX_VISIBILITY_PRESET hidden)
    list(APPEND PB_ZONDER_DOELEN dub_echo_zonder_${n})
  endforeach()
  add_executable(pb-state-toets "${PAKKET}/gereedschap/pb-state-toets.cpp")
  target_link_libraries(pb-state-toets PRIVATE clap ${CMAKE_DL_LIBS})
  set_target_properties(pb-state-toets PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  add_dependencies(pb-state-toets ${PB_ZONDER_DOELEN})
  if (PB_RTSAN)
    target_compile_options(pb-state-toets PRIVATE -fsanitize=realtime)
    target_link_options(pb-state-toets PRIVATE -fsanitize=realtime)
  endif()
endif()
