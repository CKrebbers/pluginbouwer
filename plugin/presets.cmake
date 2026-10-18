# presets.cmake — de fabriekspresets uit presets/<sleutel>.json als tabel in het binary (ARCHITECTUUR.md "Presets").
#
#   pb_fabriekspresets(<map met .json> <uitvoerbestand .inc>)
#
# Elk bestand is { "naam": "...", "uitleg": "...", "waarden": { "<parameternaam>": getal, ... } }. De sleutel (de
# bestandsnaam zonder .json, alleen a-z 0-9 -) is de load key van de preset. Een kapot bestand, een ontbrekend veld,
# een waarde die geen getal is of een dubbele naam is hier al een bouwfout (configure). Of elke parameternaam bestaat
# en elke waarde binnen het bereik ligt (en exact is in float32), controleert plugin.cpp met static_assert tegen
# Ontwerp::PARAMS: ook dat is een bouwfout, en de grenzen staan dan maar op één plek.
# De presets staan op volgorde van hun sleutel (file(GLOB) sorteert).

function(pb_cpp_tekst invoer uitvoer)
  string(REPLACE "\\" "\\\\" t "${invoer}")
  string(REPLACE "\"" "\\\"" t "${t}")
  string(REPLACE "\n" "\\n" t "${t}")
  set(${uitvoer} "\"${t}\"" PARENT_SCOPE)
endfunction()

function(pb_fabriekspresets map uit)
  file(GLOB bestanden CONFIGURE_DEPENDS "${map}/*.json")
  if (NOT bestanden)
    message(FATAL_ERROR "presets: geen .json in ${map}")
  endif()

  set(tekst "// Gegenereerd door plugin/presets.cmake uit presets/*.json. Niet met de hand veranderen.\n")
  set(tabel "")
  set(namen "")
  set(nr 0)
  foreach(bestand IN LISTS bestanden)
    get_filename_component(sleutel "${bestand}" NAME_WE)
    if (NOT sleutel MATCHES "^[a-z0-9][a-z0-9-]*$")
      message(FATAL_ERROR "presets: sleutel '${sleutel}' (${bestand}): alleen a-z, 0-9 en -")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${bestand}")  # een andere waarde: opnieuw
    file(READ "${bestand}" json)

    string(JSON naam ERROR_VARIABLE fout GET "${json}" naam)
    if (fout OR naam STREQUAL "")
      message(FATAL_ERROR "presets: ${bestand}: geen \"naam\" (${fout})")
    endif()
    if (naam IN_LIST namen)
      message(FATAL_ERROR "presets: ${bestand}: de naam '${naam}' komt twee keer voor")
    endif()
    list(APPEND namen "${naam}")
    string(JSON uitleg ERROR_VARIABLE fout GET "${json}" uitleg)
    if (fout)
      message(FATAL_ERROR "presets: ${bestand}: geen \"uitleg\" (${fout})")
    endif()
    string(JSON soort ERROR_VARIABLE fout TYPE "${json}" waarden)
    if (fout OR NOT soort STREQUAL "OBJECT")
      message(FATAL_ERROR "presets: ${bestand}: \"waarden\" ontbreekt of is geen object")
    endif()

    string(JSON n LENGTH "${json}" waarden)
    set(rij "")
    if (n GREATER 0)
      math(EXPR laatste "${n} - 1")
      foreach(k RANGE ${laatste})
        string(JSON pnaam MEMBER "${json}" waarden ${k})
        string(JSON psoort TYPE "${json}" waarden "${pnaam}")
        if (NOT psoort STREQUAL "NUMBER")
          message(FATAL_ERROR "presets: ${bestand}: de waarde van '${pnaam}' is geen getal")
        endif()
        string(JSON pwaarde GET "${json}" waarden "${pnaam}")
        pb_cpp_tekst("${pnaam}" pn)
        string(APPEND rij "    {${pn}, double(${pwaarde})},\n")
      endforeach()
    else()
      string(APPEND rij "    {nullptr, 0.0},  // geen waarden: alles naar de standaard\n")
    endif()

    pb_cpp_tekst("${naam}" cn)
    pb_cpp_tekst("${uitleg}" cu)
    string(APPEND tekst "constexpr PresetWaarde PRESET_${nr}[] = {\n${rij}};\n")
    string(APPEND tabel "    {\"${sleutel}\", ${cn}, ${cu}, PB_CLAP_ID \".preset.${sleutel}\", PRESET_${nr}, ${n}},\n")
    math(EXPR nr "${nr} + 1")
  endforeach()

  string(APPEND tekst "constexpr FabrieksPreset FABRIEKSPRESETS[] = {\n${tabel}};\n")
  # alleen schrijven als het verandert, anders bouwt alles opnieuw bij elke configure
  set(oud "")
  if (EXISTS "${uit}")
    file(READ "${uit}" oud)
  endif()
  if (NOT oud STREQUAL tekst)
    file(WRITE "${uit}" "${tekst}")
  endif()
  message(STATUS "presets: ${nr} fabriekspresets uit ${map}")
endfunction()
