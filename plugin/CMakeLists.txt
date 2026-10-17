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

# Fase 3: het frontpaneel (design.md). Eigen tekenaar (paneel.cpp, stb_truetype) met de lettertypes erin, en per
# platform een venster: Cocoa (macOS) of X11 (Linux). De DSP wordt er niet mee gecompileerd.
set(PB_LETTERTYPES ${CMAKE_CURRENT_SOURCE_DIR}/extern/lettertypes)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/lettertypes.cpp
  COMMAND ${CMAKE_COMMAND} -DUIT=${CMAKE_BINARY_DIR}/lettertypes.cpp -DMAP=${PB_LETTERTYPES}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/lettertypes.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lettertypes.cmake ${PB_LETTERTYPES}/IBMPlexMono-Regular.ttf
          ${PB_LETTERTYPES}/IBMPlexMono-Medium.ttf ${PB_LETTERTYPES}/Silkscreen-Regular.ttf
  COMMENT "lettertypes in de plugin zetten")
set(PB_PANEEL_BRONNEN bron/paneel.cpp ${CMAKE_BINARY_DIR}/lettertypes.cpp)
if (APPLE)
  list(APPEND PB_PANEEL_BRONNEN bron/venster_mac.mm)
  set_source_files_properties(bron/venster_mac.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
else()
  find_package(X11 REQUIRED)
  list(APPEND PB_PANEEL_BRONNEN bron/venster_x11.cpp)
endif()

add_library(keten_dub_echo-impl STATIC bron/plugin.cpp ${PB_PANEEL_BRONNEN})
target_link_libraries(keten_dub_echo-impl PUBLIC clap clap-wrapper-extensions PRIVATE pb-dsp)
target_compile_definitions(keten_dub_echo-impl PRIVATE PB_PANEEL=1)
target_include_directories(keten_dub_echo-impl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/extern)
if (APPLE)
  target_link_libraries(keten_dub_echo-impl PUBLIC "-framework Cocoa" "-framework CoreGraphics")
else()
  target_link_libraries(keten_dub_echo-impl PUBLIC X11::X11)
endif()
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

# Fase 4: de fabriekspresets (presets/*.json) naast de plugin, waar de preset-discovery van plugin.cpp ze zoekt:
# macOS in elke bundel (CLAP, VST3, AU) onder Contents/Resources/presets, elders de map "<naam>-presets" naast de .clap
file(GLOB PB_PRESET_JSON CONFIGURE_DEPENDS ${PB_PAKKET}/presets/*.json)
foreach(formaat clap vst3 auv2)
  set(doel keten_dub_echo_${formaat})
  if (NOT TARGET ${doel})
    continue()
  endif()
  if (APPLE)
    set(map "$<TARGET_BUNDLE_DIR:${doel}>/Contents/Resources/presets")
  elseif (formaat STREQUAL "clap")
    set(map "$<TARGET_FILE_DIR:${doel}>/$<TARGET_FILE_BASE_NAME:${doel}>-presets")
  else()
    continue()
  endif()
  add_custom_command(TARGET ${doel} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${map}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${map}"
    COMMAND ${CMAKE_COMMAND} -E copy ${PB_PRESET_JSON} "${map}/"
    COMMENT "fabriekspresets naast ${doel}"
    VERBATIM)
endforeach()

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

# Fase 2: de eigen toetsen (plugin/toets). pb-toets is een kleine CLAP-host voor
#   - de state-toets: state van een build zonder parameter N laadt in de echte build (en omgekeerd);
#   - automatisering: dezelfde waarden als events midden in blokken veranderen niets aan de null-test.
# Per parameter-id uit dsp/ontwerpen/keten_dub_echo.h een kale CLAP (build/zonder/zonder-<id>.clap) die dat id niet kent,
# zoals een oudere versie van de plugin. Die worden nooit uitgeleverd.
option(PB_TOETSEN "Bouw pb-toets en de CLAPs zonder parameter N (state-toets)" ON)
if (PB_TOETSEN)
  add_executable(pb-toets toets/pb-toets.cpp)
  target_link_libraries(pb-toets PRIVATE clap ${CMAKE_DL_LIBS})
  set_target_properties(pb-toets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if (PB_RTSAN)
    target_compile_options(pb-toets PRIVATE -fsanitize=realtime)
    target_link_options(pb-toets PRIVATE -fsanitize=realtime)
  endif()

  # de id's uit de regels "{<id>, \"<naam>\", ..." van PB_PARAMS
  file(READ ${PB_PAKKET}/dsp/ontwerpen/keten_dub_echo.h PB_ONTWERP)
  string(REGEX MATCHALL "\n *\\{[0-9]+, \"" PB_PARAM_REGELS "${PB_ONTWERP}")
  set(PB_PARAM_IDS "")
  foreach(regel IN LISTS PB_PARAM_REGELS)
    string(REGEX MATCH "[0-9]+" id "${regel}")
    list(APPEND PB_PARAM_IDS ${id})
  endforeach()
  message(STATUS "state-toets: CLAPs zonder parameter ${PB_PARAM_IDS}")
  set(PB_ZONDER_TARGETS "")
  foreach(id IN LISTS PB_PARAM_IDS)
    add_library(pb-zonder-${id}-impl STATIC bron/plugin.cpp)
    target_link_libraries(pb-zonder-${id}-impl PUBLIC clap PRIVATE pb-dsp)
    target_compile_definitions(pb-zonder-${id}-impl PRIVATE PB_ZONDER_PARAM=${id})
    add_library(pb-zonder-${id} MODULE bron/entry.cpp)
    target_link_libraries(pb-zonder-${id} PRIVATE pb-zonder-${id}-impl)
    set_target_properties(pb-zonder-${id} PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "zonder-${id}"
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/zonder)
    list(APPEND PB_ZONDER_TARGETS pb-zonder-${id})
  endforeach()
  add_custom_target(pb-toetsen DEPENDS pb-toets ${PB_ZONDER_TARGETS})

  # Fase 3: pb-paneel opent het frontpaneel in een echt venster (Cocoa of X11), bedient het met de muis, toetst wat
  # de plugin aan de host meldt, en maakt schermafbeeldingen (plugin/toets/toets-fase3.sh).
  add_executable(pb-paneel toets/pb-paneel.cpp)
  target_link_libraries(pb-paneel PRIVATE clap ${CMAKE_DL_LIBS})
  set_target_properties(pb-paneel PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if (APPLE)
    set_source_files_properties(toets/pb-paneel.cpp PROPERTIES LANGUAGE OBJCXX COMPILE_OPTIONS "-fno-objc-arc")
    target_link_libraries(pb-paneel PRIVATE "-framework Cocoa" "-framework CoreGraphics")
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(pb-paneel PRIVATE X11::X11 Threads::Threads)
  endif()
  if (PB_RTSAN)
    target_compile_options(pb-paneel PRIVATE -fsanitize=realtime)
    target_link_options(pb-paneel PRIVATE -fsanitize=realtime)
  endif()
  add_dependencies(pb-toetsen pb-paneel)
endif()
