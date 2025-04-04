set(AIGER_LIB_SOURCES deps/aiger/aiger.c)
set(AIGER_LIB_HEADERS deps/aiger/aiger.h)
add_library(aiger-lib-static STATIC ${AIGER_LIB_SOURCES})
target_include_directories(aiger-lib-static PUBLIC deps/aiger/)
set_target_properties(aiger-lib-static PROPERTIES
  OUTPUT_NAME "aiger"
  PUBLIC_HEADER "${AIGER_LIB_HEADERS}")
