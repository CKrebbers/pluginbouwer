# bytes.cmake — maakt van binaire bestanden een C-bron met byte-rijen (de lettertypen van het paneel).
#   cmake -DUIT=<uit.c> "-DBRONNEN=naam=pad;naam2=pad2" -P bytes.cmake
# Per bestand: const unsigned char pb_lettertype_<naam>[] en const unsigned long pb_lettertype_<naam>_n.
set(c "/* gegenereerd door plugin/cmake/bytes.cmake; niet met de hand wijzigen */\n")
foreach(paar IN LISTS BRONNEN)
  string(FIND "${paar}" "=" is)
  string(SUBSTRING "${paar}" 0 ${is} naam)
  math(EXPR na "${is} + 1")
  string(SUBSTRING "${paar}" ${na} -1 pad)
  file(READ "${pad}" hex HEX)
  string(LENGTH "${hex}" lengte)
  math(EXPR n "${lengte} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
  string(APPEND c "const unsigned char pb_lettertype_${naam}[${n}] = {\n${bytes}\n};\n")
  string(APPEND c "const unsigned long pb_lettertype_${naam}_n = ${n};\n")
endforeach()
file(WRITE "${UIT}" "${c}")
