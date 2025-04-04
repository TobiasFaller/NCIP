cmake_minimum_required(VERSION 3.22.1)

project(minisat)

find_package(ZLIB)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(LTO                   "Enable Link-Time Optimization"                            ON)
option(STATIC_BINARIES       "Link binaries statically."                                ON)
option(FULLY_STATIC_BINARIES "Link binaries fully statically (including libc++, etc.)." ON)
option(USE_SORELEASE         "Use SORELEASE in shared library filename."                ON)

#--------------------------------------------------------------------------------------------------
# Library version:

set(MINISAT_SOMAJOR   2)
set(MINISAT_SOMINOR   1)
set(MINISAT_SORELEASE 0)

# Compute VERSION and SOVERSION:
if (USE_SORELEASE)
  set(MINISAT_VERSION ${MINISAT_SOMAJOR}.${MINISAT_SOMINOR}.${MINISAT_SORELEASE})
else()
  set(MINISAT_VERSION ${MINISAT_SOMAJOR}.${MINISAT_SOMINOR})
endif()
set(MINISAT_SOVERSION ${MINISAT_SOMAJOR})

#--------------------------------------------------------------------------------------------------
# Compile flags:

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)

#--------------------------------------------------------------------------------------------------
# Library Targets:

set(MINISAT_LIB_SOURCES
  minisat/utils/Options.cc
  minisat/utils/System.cc
  minisat/core/Solver.cc
  minisat/simp/SimpSolver.cc
  minisat/preproc/Preprocessor.cc)
set(MINISAT_LIB_HEADER_DIRS
  minisat/mtl
  minisat/utils
  minisat/simp
  minisat/core
  minisat/preproc)
add_library(minisat-lib-static STATIC ${MINISAT_LIB_SOURCES})
add_library(minisat-lib-shared SHARED ${MINISAT_LIB_SOURCES})
target_include_directories(minisat-lib-static PRIVATE ${ZLIB_INCLUDE_DIR} PUBLIC minisat)
target_include_directories(minisat-lib-shared PRIVATE ${ZLIB_INCLUDE_DIR} PUBLIC minisat)
target_link_libraries(minisat-lib-shared ${ZLIB_LIBRARY})
target_link_libraries(minisat-lib-static ${ZLIB_LIBRARY})
set_target_properties(minisat-lib-static PROPERTIES
  OUTPUT_NAME "minisat")
set_target_properties(minisat-lib-shared PROPERTIES
  OUTPUT_NAME "minisat"
  VERSION ${MINISAT_VERSION}
  SOVERSION ${MINISAT_SOVERSION})
install(TARGETS minisat-lib-static minisat-lib-shared)
install(DIRECTORY ${MINISAT_LIB_HEADER_DIRS}
  DESTINATION include/minisat
  FILES_MATCHING PATTERN "*.h")

#--------------------------------------------------------------------------------------------------
# Executable Targets:

if(ALL_STATIC_BINARIES)
  set(MINISAT_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(ALL_STATIC_BINARIES)
  set(MINISAT_BIN_LINK "")
endif(ALL_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(MINISAT_LIB_LINK "static")
else(STATIC_BINARIES)
  set(MINISAT_LIB_LINK "shared")
endif(STATIC_BINARIES)

add_executable(minisat-core minisat/core/Main.cc)
target_link_libraries(minisat-core minisat-lib-${MINISAT_LIB_LINK})
target_link_options(minisat-core PRIVATE ${MINISAT_BIN_LINK})
set_target_properties(minisat-core PROPERTIES OUTPUT_NAME "minisat-core")
install(TARGETS minisat-core)

add_executable(minisat-simp minisat/simp/Main.cc)
target_link_libraries(minisat-simp minisat-lib-${MINISAT_LIB_LINK})
target_link_options(minisat-simp PRIVATE ${MINISAT_BIN_LINK})
set_target_properties(minisat-simp PROPERTIES OUTPUT_NAME "minisat-simp")
install(TARGETS minisat-simp)
