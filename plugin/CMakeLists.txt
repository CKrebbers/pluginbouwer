# Dub-echo — de CLAP-schil rond het Ontwerp uit dsp/, en via clap-wrapper ook VST3 en AUv2.
#   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
# Uitvoer: build/plugins/PB-Dub-echo.{clap,vst3,component} en build/pb-render (de CLAP-host voor de null-test).
cmake_minimum_required(VERSION 3.21)
project(dub_echo VERSION 0.1.0 LANGUAGES C CXX)

# valkuil 3: zonder deze talen ontbreekt CMAKE_OBJCXX_COMPILE_OBJECT voor de AU-wrapper
if (APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
endif()

get_filename_component(PB_PAKKET "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(PB_RTSAN "pb-render bouwen met -fsanitize=realtime (clang >= 20)" OFF)
option(PB_KANARIE "ook een kanarie-CLAP bouwen die in process alloceert (RTSan moet hem vangen)" OFF)

# clap-wrapper v0.16.0 met de patches uit patches/ (in naamvolgorde; overgeslagen als ze er al op zitten)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 ON CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_OUTPUT_NAME "PB-Dub-echo" CACHE STRING "" FORCE)
include(FetchContent)
FetchContent_Declare(clap-wrapper
  GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
  GIT_TAG v0.16.0
  GIT_SHALLOW TRUE
  PATCH_COMMAND sh ${PB_PAKKET}/gereedschap/pas-patches-toe.sh ${PB_PAKKET}/patches
)
FetchContent_MakeAvailable(clap-wrapper)

# vaste regels voor de DSP: bit voor bit gelijk aan de WASM van de tool
set(PB_DSP_VLAGGEN -ffp-contract=off -fno-fast-math -fno-math-errno)

add_library(dub_echo_impl STATIC dub_echo.cpp)
target_include_directories(dub_echo_impl PUBLIC ${PB_PAKKET}/dsp)
target_link_libraries(dub_echo_impl PUBLIC clap)
target_compile_options(dub_echo_impl PRIVATE ${PB_DSP_VLAGGEN})

# fase 3: het frontpaneel (design.md) als NSView voor de CLAP gui-extensie (api "cocoa"); clap-wrapper geeft het door
# aan de AU en de VST3. De lettertypen (IBM Plex Mono, Silkscreen; OFL) gaan als bytes het binary in.
if (APPLE)
  set(PB_LETTERS ${CMAKE_CURRENT_SOURCE_DIR}/ui/lettertypen)
  set(PB_LETTERS_CPP ${CMAKE_BINARY_DIR}/lettertypen.cpp)
  set(PB_LETTERS_INHOUD "// gemaakt door plugin/CMakeLists.txt uit plugin/ui/lettertypen/ (niet bewerken)\n")
  foreach(PB_L SILKSCREEN:Silkscreen-Regular.ttf PLEXMONO:IBMPlexMono-Medium.ttf)
    string(REPLACE ":" ";" PB_L "${PB_L}")
    list(GET PB_L 0 PB_L_NAAM)
    list(GET PB_L 1 PB_L_BESTAND)
    file(READ ${PB_LETTERS}/${PB_L_BESTAND} PB_L_HEX HEX)
    string(LENGTH "${PB_L_HEX}" PB_L_LEN)
    math(EXPR PB_L_LEN "${PB_L_LEN} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," PB_L_HEX "${PB_L_HEX}")
    string(APPEND PB_LETTERS_INHOUD "extern const unsigned char PB_LETTER_${PB_L_NAAM}[] = {${PB_L_HEX}};\n")
    string(APPEND PB_LETTERS_INHOUD "extern const unsigned long PB_LETTER_${PB_L_NAAM}_N = ${PB_L_LEN};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PB_LETTERS}/${PB_L_BESTAND})
  endforeach()
  file(CONFIGURE OUTPUT ${PB_LETTERS_CPP} CONTENT "${PB_LETTERS_INHOUD}" @ONLY)
  target_sources(dub_echo_impl PRIVATE ui/paneel_mac.mm ${PB_LETTERS_CPP})
  set_source_files_properties(ui/paneel_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(dub_echo_impl PRIVATE PB_MET_UI=1)
  target_link_libraries(dub_echo_impl PUBLIC "-framework AppKit" "-framework CoreText" "-framework CoreGraphics")
endif()

make_clapfirst_plugins(
  TARGET_NAME dub_echo
  IMPL_TARGET dub_echo_impl
  OUTPUT_NAME "PB-Dub-echo"
  ENTRY_SOURCE entry.cpp
  BUNDLE_IDENTIFIER "nl.varve.pluginbouwer.dub-echo"
  BUNDLE_VERSION ${PROJECT_VERSION}
  COPY_AFTER_BUILD FALSE
  PLUGIN_FORMATS CLAP VST3 AUV2
  ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
  AUV2_MANUFACTURER_NAME "Varve"
  AUV2_MANUFACTURER_CODE "Vrve"
  AUV2_SUBTYPE_CODE "PDca"
  AUV2_INSTRUMENT_TYPE "aufx"
)

# valkuil 2: statics van de VST3 SDK draaien bij exit anders in uitgeladen geheugen (crash na SUCCESS in pluginval)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(dub_echo_vst3 PRIVATE -Wl,-z,nodelete)
endif()

# gui-toets (macOS): opent het paneel van de .clap in een venster, sleept/dubbelklikt met nagemaakte muis-events,
# eist dat de gebaren en waarden via params.flush bij de host aankomen, en bewaart schermafbeeldingen (PNG).
if (APPLE)
  add_executable(gui-toets toetsen/gui-toets.mm)
  target_include_directories(gui-toets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(gui-toets PRIVATE clap "-framework AppKit")
  set_source_files_properties(toetsen/gui-toets.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_target_properties(gui-toets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# pb-render: de kleine CLAP-host uit gereedschap/ (null-test en RTSan)
add_executable(pb-render ${PB_PAKKET}/gereedschap/pb-render.cpp)
target_link_libraries(pb-render PRIVATE clap ${CMAKE_DL_LIBS})
set_target_properties(pb-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (PB_RTSAN)
  target_compile_options(pb-render PRIVATE -fsanitize=realtime)
  target_link_options(pb-render PRIVATE -fsanitize=realtime)
endif()

# de kanarie: dezelfde plugin, maar process alloceert. Een RTSan-run die hem niet vangt bewijst niets.
if (PB_KANARIE)
  add_library(dub_echo_kanarie MODULE dub_echo.cpp entry.cpp)
  target_include_directories(dub_echo_kanarie PRIVATE ${PB_PAKKET}/dsp)
  target_link_libraries(dub_echo_kanarie PRIVATE clap)
  target_compile_options(dub_echo_kanarie PRIVATE ${PB_DSP_VLAGGEN})
  target_compile_definitions(dub_echo_kanarie PRIVATE PB_KANARIE=1)
  set_target_properties(dub_echo_kanarie PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "PB-Dub-echo-kanarie"
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kanarie)
endif()

# fase 2: de eigen toetsen (plugin/toetsen/param-toets.cpp) en "oudere" builds zonder één parameter, voor de toets
# "state van een build zonder parameter N laadt in een build mét". Elke id uit SPEC.md krijgt zijn eigen build.
option(PB_TOETSEN "param-toets en de builds zonder parameter N bouwen" ON)
if (PB_TOETSEN)
  add_executable(param-toets toetsen/param-toets.cpp)
  target_link_libraries(param-toets PRIVATE clap ${CMAKE_DL_LIBS})
  set_target_properties(param-toets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if (PB_RTSAN)
    target_compile_options(param-toets PRIVATE -fsanitize=realtime)
    target_link_options(param-toets PRIVATE -fsanitize=realtime)
  endif()
  # fase 3: de plugin-kant van het paneel (gui-extensie, Bediening, wachtrij) zonder Cocoa, voor ui-toets op elk
  # platform en onder RTSan (toetsen/ui-stub.cpp is de "view")
  add_library(dub_echo_ui_stub MODULE dub_echo.cpp entry.cpp toetsen/ui-stub.cpp)
  target_include_directories(dub_echo_ui_stub PRIVATE ${PB_PAKKET}/dsp ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(dub_echo_ui_stub PRIVATE clap)
  target_compile_options(dub_echo_ui_stub PRIVATE ${PB_DSP_VLAGGEN})
  target_compile_definitions(dub_echo_ui_stub PRIVATE PB_MET_UI=1)
  set_target_properties(dub_echo_ui_stub PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "PB-Dub-echo-ui-stub"
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/toetsen)
  add_executable(ui-toets toetsen/ui-toets.cpp)
  target_link_libraries(ui-toets PRIVATE clap ${CMAKE_DL_LIBS})
  set_target_properties(ui-toets PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if (PB_RTSAN)
    target_compile_options(ui-toets PRIVATE -fsanitize=realtime)
    target_link_options(ui-toets PRIVATE -fsanitize=realtime)
  endif()
  foreach(PB_ID RANGE 1 8)
    add_library(dub_echo_zonder_${PB_ID} MODULE dub_echo.cpp entry.cpp)
    target_include_directories(dub_echo_zonder_${PB_ID} PRIVATE ${PB_PAKKET}/dsp)
    target_link_libraries(dub_echo_zonder_${PB_ID} PRIVATE clap)
    target_compile_options(dub_echo_zonder_${PB_ID} PRIVATE ${PB_DSP_VLAGGEN})
    target_compile_definitions(dub_echo_zonder_${PB_ID} PRIVATE PB_ZONDER_PARAM=${PB_ID})
    set_target_properties(dub_echo_zonder_${PB_ID} PROPERTIES PREFIX "" SUFFIX ".clap"
                          OUTPUT_NAME "PB-Dub-echo-zonder-${PB_ID}" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/toetsen)
  endforeach()
endif()
