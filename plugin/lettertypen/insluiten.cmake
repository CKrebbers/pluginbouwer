# insluiten.cmake — zet de lettertypen als bytes in een .cpp (voor paneel_mac.mm).
#   cmake -DUIT=<lettertypen.cpp> -DBRONNEN="<a.ttf>|<b.ttf>" -P insluiten.cmake
# Per bestand: extern const unsigned char pbLetter_<naam>[] en extern const unsigned long pbLetter_<naam>_lengte,
# met <naam> de bestandsnaam zonder extensie, alleen letters en cijfers (IBMPlexMonoRegular, SilkscreenRegular).
string(REPLACE "|" ";" BRONNEN "${BRONNEN}")
set(tekst "// gemaakt door lettertypen/insluiten.cmake; niet bewerken\n")
foreach(bron IN LISTS BRONNEN)
  get_filename_component(naam "${bron}" NAME_WE)
  string(REGEX REPLACE "[^A-Za-z0-9]" "" naam "${naam}")
  file(READ "${bron}" hex HEX)
  string(LENGTH "${hex}" n)
  math(EXPR lengte "${n} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string(APPEND tekst "extern const unsigned char pbLetter_${naam}[] = {${bytes}};\n")
  string(APPEND tekst "extern const unsigned long pbLetter_${naam}_lengte = ${lengte};\n")
endforeach()
file(WRITE "${UIT}" "${tekst}")
