cmake_minimum_required(VERSION 3.22.1)

project(cadicraig)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(LTO                      "Enable Link-Time Optimization"                            ON )
option(STATIC_BINARIES          "Link binaries statically."                                ON )
option(FULLY_STATIC_BINARIES    "Link binaries fully statically (including libc++, etc.)." ON )
option(USE_SORELEASE            "Use SORELEASE in shared library filename."                ON )
OPTION(CADICAL_LOGGING          "Enable logging output"                                    OFF)
OPTION(CADICAL_TIMESTAMP        "Enable compile timestamp"                                 OFF)

#--------------------------------------------------------------------------------------------------
# Library version:

set(CADICRAIG_SOMAJOR   2)
set(CADICRAIG_SOMINOR   0)
set(CADICRAIG_SORELEASE 0)

# Compute VERSION and SOVERSION:
if (USE_SORELEASE)
  set(CADICRAIG_VERSION ${CADICRAIG_SOMAJOR}.${CADICRAIG_SOMINOR}.${CADICRAIG_SORELEASE})
else()
  set(CADICRAIG_VERSION ${CADICRAIG_SOMAJOR}.${CADICRAIG_SOMINOR})
endif()
set(CADICRAIG_SOVERSION ${CADICRAIG_SOMAJOR})

#--------------------------------------------------------------------------------------------------
# Compile flags:

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)

#--------------------------------------------------------------------------------------------------
# Library Targets:

set(CADICAL_DIR ${PROJECT_SOURCE_DIR}/../cadical)
set(CADICAL_LIB_SOURCES
  ${CADICAL_DIR}/src/analyze.cpp
  ${CADICAL_DIR}/src/arena.cpp
  ${CADICAL_DIR}/src/assume.cpp
  ${CADICAL_DIR}/src/averages.cpp
  ${CADICAL_DIR}/src/backtrack.cpp
  ${CADICAL_DIR}/src/backward.cpp
  ${CADICAL_DIR}/src/bins.cpp
  ${CADICAL_DIR}/src/block.cpp
  ${CADICAL_DIR}/src/ccadical.cpp
  ${CADICAL_DIR}/src/checker.cpp
  ${CADICAL_DIR}/src/clause.cpp
  ${CADICAL_DIR}/src/collect.cpp
  ${CADICAL_DIR}/src/compact.cpp
  ${CADICAL_DIR}/src/condition.cpp
  ${CADICAL_DIR}/src/config.cpp
  ${CADICAL_DIR}/src/congruence.cpp
  ${CADICAL_DIR}/src/constrain.cpp
  ${CADICAL_DIR}/src/contract.cpp
  ${CADICAL_DIR}/src/cover.cpp
  ${CADICAL_DIR}/src/decide.cpp
  ${CADICAL_DIR}/src/decompose.cpp
  ${CADICAL_DIR}/src/deduplicate.cpp
  ${CADICAL_DIR}/src/definition.cpp
  ${CADICAL_DIR}/src/drattracer.cpp
  ${CADICAL_DIR}/src/elim.cpp
  ${CADICAL_DIR}/src/elimfast.cpp
  ${CADICAL_DIR}/src/ema.cpp
  ${CADICAL_DIR}/src/extend.cpp
  ${CADICAL_DIR}/src/external.cpp
  ${CADICAL_DIR}/src/external_propagate.cpp
  ${CADICAL_DIR}/src/factor.cpp
  ${CADICAL_DIR}/src/file.cpp
  ${CADICAL_DIR}/src/flags.cpp
  ${CADICAL_DIR}/src/flip.cpp
  ${CADICAL_DIR}/src/format.cpp
  ${CADICAL_DIR}/src/frattracer.cpp
  ${CADICAL_DIR}/src/gates.cpp
  ${CADICAL_DIR}/src/instantiate.cpp
  ${CADICAL_DIR}/src/internal.cpp
  ${CADICAL_DIR}/src/idruptracer.cpp
  ${CADICAL_DIR}/src/lidruptracer.cpp
  ${CADICAL_DIR}/src/limit.cpp
  ${CADICAL_DIR}/src/logging.cpp
  ${CADICAL_DIR}/src/lookahead.cpp
  ${CADICAL_DIR}/src/lratchecker.cpp
  ${CADICAL_DIR}/src/lrattracer.cpp
  ${CADICAL_DIR}/src/lucky.cpp
  ${CADICAL_DIR}/src/message.cpp
  ${CADICAL_DIR}/src/minimize.cpp
  ${CADICAL_DIR}/src/occs.cpp
  ${CADICAL_DIR}/src/options.cpp
  ${CADICAL_DIR}/src/parse.cpp
  ${CADICAL_DIR}/src/phases.cpp
  ${CADICAL_DIR}/src/probe.cpp
  ${CADICAL_DIR}/src/profile.cpp
  ${CADICAL_DIR}/src/proof.cpp
  ${CADICAL_DIR}/src/propagate.cpp
  ${CADICAL_DIR}/src/queue.cpp
  ${CADICAL_DIR}/src/random.cpp
  ${CADICAL_DIR}/src/reap.cpp
  ${CADICAL_DIR}/src/reduce.cpp
  ${CADICAL_DIR}/src/rephase.cpp
  ${CADICAL_DIR}/src/report.cpp
  ${CADICAL_DIR}/src/resources.cpp
  ${CADICAL_DIR}/src/restart.cpp
  ${CADICAL_DIR}/src/restore.cpp
  ${CADICAL_DIR}/src/score.cpp
  ${CADICAL_DIR}/src/shrink.cpp
  ${CADICAL_DIR}/src/signal.cpp
  ${CADICAL_DIR}/src/solution.cpp
  ${CADICAL_DIR}/src/solver.cpp
  ${CADICAL_DIR}/src/stable.cpp
  ${CADICAL_DIR}/src/stats.cpp
  ${CADICAL_DIR}/src/subsume.cpp
  ${CADICAL_DIR}/src/sweep.cpp
  ${CADICAL_DIR}/src/terminal.cpp
  ${CADICAL_DIR}/src/ternary.cpp
  ${CADICAL_DIR}/src/tier.cpp
  ${CADICAL_DIR}/src/transred.cpp
  ${CADICAL_DIR}/src/unstable.cpp
  ${CADICAL_DIR}/src/util.cpp
  ${CADICAL_DIR}/src/var.cpp
  ${CADICAL_DIR}/src/veripbtracer.cpp
  ${CADICAL_DIR}/src/version.cpp
  ${CADICAL_DIR}/src/vivify.cpp
  ${CADICAL_DIR}/src/walk.cpp
  ${CADICAL_DIR}/src/watch.cpp
  ${CADICAL_DIR}/src/kitten.c)
set(CADICAL_LIB_HEADERS
  ${CADICAL_DIR}/src/ccadical.h
  ${CADICAL_DIR}/src/cadical.hpp)
add_library(cadical-lib-static STATIC ${CADICAL_LIB_SOURCES})
add_library(cadical-lib-shared SHARED ${CADICAL_LIB_SOURCES})
target_include_directories(cadical-lib-static PUBLIC ${CADICAL_DIR}/src)
target_include_directories(cadical-lib-shared PUBLIC ${CADICAL_DIR}/src)
set_target_properties(cadical-lib-static PROPERTIES
  OUTPUT_NAME "cadical"
  PUBLIC_HEADER "${CADICAL_LIB_HEADERS}")
set_target_properties(cadical-lib-shared PROPERTIES
  OUTPUT_NAME "cadical"
  PUBLIC_HEADER "${CADICAL_LIB_HEADERS}")
install(TARGETS cadical-lib-static cadical-lib-shared
  PUBLIC_HEADER DESTINATION include/cadical)

set(IPASIRCADICAL_LIB_SOURCES
  ${CADICAL_DIR}/src/ipasir.cpp)
set(IPASIRCADICAL_LIB_HEADERS
  ${CADICAL_DIR}/src/ipasir.h)
add_library(ipasircadical-lib-static STATIC ${IPASIRCADICAL_LIB_SOURCES})
add_library(ipasircadical-lib-shared SHARED ${IPASIRCADICAL_LIB_SOURCES})
target_include_directories(ipasircadical-lib-static PUBLIC ${CADICAL_DIR}/src)
target_include_directories(ipasircadical-lib-shared PUBLIC ${CADICAL_DIR}/src)
target_link_libraries(ipasircadical-lib-static PRIVATE cadical-lib-static)
target_link_libraries(ipasircadical-lib-shared PRIVATE cadical-lib-shared)
set_target_properties(ipasircadical-lib-static PROPERTIES
  OUTPUT_NAME "ipasircadical"
  PUBLIC_HEADER "${IPASIRCADICAL_LIB_HEADERS}")
set_target_properties(ipasircadical-lib-shared PROPERTIES
  OUTPUT_NAME "ipasircadical"
  PUBLIC_HEADER "${IPASIRCADICAL_LIB_HEADERS}")
install(TARGETS ipasircadical-lib-static ipasircadical-lib-shared
  PUBLIC_HEADER DESTINATION include/cadical)

file(READ "${CADICAL_DIR}/VERSION" NEWLINE_CONSUME VERSION)
execute_process(COMMAND ${CADICAL_DIR}/scripts/get-git-id.sh OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE GIT_ID)
get_target_property(MAIN_CFLAGS cadical-lib-static COMPILE_OPTIONS_STATIC)
get_target_property(MAIN_CFLAGS cadical-lib-shared COMPILE_OPTIONS_SHARED)

add_definitions("-DVERSION=\"${VERSION}\"")
add_definitions("-DIDENTIFIER=\"${GIT_ID}\"")
add_definitions("-DCOMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\"")
add_definitions("-DFLAGS=\"${COMPILE_OPTIONS_STATIC}\"")

IF(CADICAL_LOGGING)
    add_definitions("-DLOGGING")
ENDIF(CADICAL_LOGGING)

IF(CADICAL_TIMESTAMP)
    string(TIMESTAMP TODAY "%Y-%m-%dT%H:%M:%SZ")
    add_definitions("-DDATE=\"${TODAY} UTC ${CMAKE_SYSTEM_NAME}\"")
ELSE(CADICAL_TIMESTAMP)
    add_definitions("-DDATE=\"Timestamp removed for build-cache\"")
ENDIF(CADICAL_TIMESTAMP)

if(FULLY_STATIC_BINARIES)
  set(CADICAL_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(CADICAL_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(CADICAL_LIB_LINK "static")
else(STATIC_BINARIES)
  set(CADICAL_LIB_LINK "shared")
endif(STATIC_BINARIES)

add_executable(cadical
  ${CADICAL_DIR}/src/cadical.cpp)
target_link_libraries(cadical cadical-lib-${CADICAL_LIB_LINK} cadical-lib-${CADICAL_LIB_LINK})
target_link_options(cadical PRIVATE ${CADICAL_BIN_LINK})
set_target_properties(cadical PROPERTIES OUTPUT_NAME "cadical")
install(TARGETS cadical)

#--------------------------------------------------------------------------------------------------

set(CADICRAIG_LIB_SOURCES
  ${CADICAL_DIR}/contrib/craigtracer.cpp)
set(CADICRAIG_LIB_HEADERS
  ${CADICAL_DIR}/contrib/craigtracer.hpp)
add_library(cadicraig-lib-static STATIC ${CADICRAIG_LIB_SOURCES})
add_library(cadicraig-lib-shared SHARED ${CADICRAIG_LIB_SOURCES})
target_include_directories(cadicraig-lib-static PUBLIC ${CADICAL_DIR}/contrib)
target_include_directories(cadicraig-lib-shared PUBLIC ${CADICAL_DIR}/contrib)
target_link_libraries(cadicraig-lib-static cadical-lib-static)
target_link_libraries(cadicraig-lib-shared cadical-lib-shared)
set_target_properties(cadicraig-lib-static PROPERTIES
  OUTPUT_NAME "cadicraig"
  PUBLIC_HEADER "${CADICRAIG_LIB_HEADERS}")
set_target_properties(cadicraig-lib-shared PROPERTIES
  OUTPUT_NAME "cadicraig"
  PUBLIC_HEADER "${CADICRAIG_LIB_HEADERS}"
  VERSION ${CADICRAIG_VERSION}
  SOVERSION ${CADICRAIG_SOVERSION})
install(TARGETS cadicraig-lib-static cadicraig-lib-shared
  PUBLIC_HEADER DESTINATION include/cadicraig)

#--------------------------------------------------------------------------------------------------
# Executable Targets:

if(FULLY_STATIC_BINARIES)
  set(CADICRAIG_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(CADICRAIG_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(CADICRAIG_LIB_LINK "static")
else(STATIC_BINARIES)
  set(CADICRAIG_LIB_LINK "shared")
endif(STATIC_BINARIES)

add_executable(cadicraig
  src/craigfile.cpp
  src/craigconfig.cpp
  src/craigoptions.cpp
  src/craigmessage.cpp
  src/craigparse.cpp
  src/cadicraig.cpp)
target_link_libraries(cadicraig cadical-lib-${CADICRAIG_LIB_LINK} cadicraig-lib-${CADICRAIG_LIB_LINK})
target_link_options(cadicraig PRIVATE ${CADICRAIG_BIN_LINK})
set_target_properties(cadicraig PROPERTIES OUTPUT_NAME "cadicraig")
install(TARGETS cadicraig)
