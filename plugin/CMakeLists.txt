# CMake voor de plugin: CLAP-first, clap-wrapper maakt er VST3 en AUv2 (macOS) van.
#   cmake -B build -S plugin -DCMAKE_BUILD_TYPE=Release [-DPB_ONTWERP=pijplijn_biquad]
#   cmake --build build --config Release
# Alle SDK's komen van GitHub op een vaste versie (CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES); zie BRONNEN.md.
cmake_minimum_required(VERSION 3.21)

set(PB_ONTWERP "pijplijn_biquad" CACHE STRING "Bestandsnaam in dsp/ontwerpen/ zonder .h")
set(PB_ONTWERP_H "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/ontwerpen/${PB_ONTWERP}.h")
if (NOT EXISTS "${PB_ONTWERP_H}")
  message(FATAL_ERROR "Ontwerp ${PB_ONTWERP_H} bestaat niet")
endif()

# Namen en AU-codes staan op één plek: de #defines in het ontwerp.
function(pb_lees_define naam uit)
  file(STRINGS "${PB_ONTWERP_H}" regel REGEX "^#define ${naam} \"")
  string(REGEX REPLACE "^#define ${naam} \"([^\"]*)\".*" "\\1" waarde "${regel}")
  if ("${waarde}" STREQUAL "")
    message(FATAL_ERROR "${naam} ontbreekt in ${PB_ONTWERP_H}")
  endif()
  set(${uit} "${waarde}" PARENT_SCOPE)
endfunction()
pb_lees_define(PB_NAAM PB_NAAM)
pb_lees_define(PB_CLAP_ID PB_CLAP_ID)
pb_lees_define(PB_VERSIE PB_VERSIE)
pb_lees_define(PB_AU_MAKER PB_AU_MAKER)
pb_lees_define(PB_AU_SUBTYPE PB_AU_SUBTYPE)

if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Laagste macOS (M1)")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(pluginbouwer-${PB_ONTWERP} VERSION ${PB_VERSIE} LANGUAGES C CXX)
if (APPLE)
  # de AUv2- en VST3-schillen van clap-wrapper hebben .mm-bestanden; die targets staan in ONZE map
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 TRUE CACHE BOOL "" FORCE)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(clap-wrapper)

# Regel 2 uit CLAUDE.md: dezelfde vlaggen als de WASM, zodat de DSP bit voor bit hetzelfde rekent.
set(PB_DSP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)

add_library(pb-impl STATIC src/plugin.cpp)
target_include_directories(pb-impl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../dsp src)
target_compile_definitions(pb-impl PRIVATE PB_ONTWERP="ontwerpen/${PB_ONTWERP}.h")
target_compile_options(pb-impl PRIVATE ${PB_DSP_VLAGGEN})
target_link_libraries(pb-impl PUBLIC clap clap-wrapper-extensions)
if (PB_RTSAN_KANARIE)
  target_compile_definitions(pb-impl PRIVATE PB_RTSAN_KANARIE=1)
endif()
if (PB_SANITIZE)
  # bijvoorbeeld -DPB_SANITIZE=realtime (RTSan, clang ≥ 20) of address,undefined
  target_compile_options(pb-impl PRIVATE -fsanitize=${PB_SANITIZE} -fno-omit-frame-pointer)
  target_link_options(pb-impl PUBLIC -fsanitize=${PB_SANITIZE})
endif()

set(PB_FORMATEN CLAP VST3)
if (APPLE)
  list(APPEND PB_FORMATEN AUV2)
endif()

make_clapfirst_plugins(
  TARGET_NAME pb
  IMPL_TARGET pb-impl
  OUTPUT_NAME "${PB_NAAM}"
  ENTRY_SOURCE src/entry.cpp
  BUNDLE_IDENTIFIER "${PB_CLAP_ID}"
  BUNDLE_VERSION ${PB_VERSIE}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS ${PB_FORMATEN}
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "${PB_AU_MAKER}"
  AUV2_SUBTYPE_CODE "${PB_AU_SUBTYPE}"
  AUV2_INSTRUMENT_TYPE "aufx"
)

# De render-host voor de null-test (CLAP), los van de plugin.
add_executable(pb-render ${CMAKE_CURRENT_SOURCE_DIR}/../host/pb-render.cpp)
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
if (PB_SANITIZE)
  target_compile_options(pb-render PRIVATE -fsanitize=${PB_SANITIZE} -fno-omit-frame-pointer)
  target_link_options(pb-render PRIVATE -fsanitize=${PB_SANITIZE})
endif()

# Linux: de VST3 SDK zet functie-lokale statics (Steinberg::StringConvert) met een destructor via __cxa_atexit.
# JUCE (pluginval, en hosts op JUCE) dlclose't de module, en bij exit draait die destructor in uitgeladen geheugen:
# een segfault NA "SUCCESS" (pluginval 2.0.0, gezien 15 okt 2026, backtrace in __run_exit_handlers).
# -z nodelete houdt de module geladen tot het proces stopt; dan draaien de destructors op een geldig adres.
if (UNIX AND NOT APPLE AND TARGET pb_vst3)
  target_link_options(pb_vst3 PRIVATE -Wl,-z,nodelete)
endif()
