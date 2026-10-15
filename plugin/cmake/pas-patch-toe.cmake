# pas-patch-toe.cmake — PATCH_COMMAND voor FetchContent: past PATCH toe in de huidige map, maar slaat over als hij
# er al op zit (FetchContent kan de patch-stap bij een nieuwe configure opnieuw draaien; git apply faalt dan).
#   cmake -DPATCH=<bestand> -P pas-patch-toe.cmake
if(NOT PATCH)
  message(FATAL_ERROR "pas-patch-toe: zet -DPATCH=<bestand>")
endif()
execute_process(COMMAND git apply --reverse --check "${PATCH}" RESULT_VARIABLE al_toegepast OUTPUT_QUIET ERROR_QUIET)
if(al_toegepast EQUAL 0)
  message(STATUS "patch zit er al op: ${PATCH}")
  return()
endif()
execute_process(COMMAND git apply "${PATCH}" RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "patch past niet: ${PATCH}")
endif()
message(STATUS "patch toegepast: ${PATCH}")
