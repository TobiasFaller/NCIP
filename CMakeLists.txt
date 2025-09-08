cmake_minimum_required(VERSION 3.22.1)
include(FetchContent)

project(ncip)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(LTO                          "Enable Link-Time Optimization"                            ON )
option(STATIC_BINARIES              "Link binaries statically."                                ON )
option(FULLY_STATIC_BINARIES        "Link binaries fully statically (including libc++, etc.)." ON )
option(USE_SORELEASE                "Use SORELEASE in shared library filename."                ON )
option(NCIP_BACKEND_MINICRAIG       "Build NCIP with MiniCraig backend."                       ON )
option(NCIP_BACKEND_MINICRAIG_DEBUG "Build NCIP with MiniCraig Debug backend."                 OFF)
option(NCIP_BACKEND_CADICRAIG       "Build NCIP with CaDiCraig backend."                       ON )
option(NCIP_BACKEND_CADICRAIG_DEBUG "Build NCIP with CaDiCraig Debug backend."                 OFF)
option(NCIP_BACKEND_KITTENCRAIG     "Build NCIP with KittenCraig backend."                     ON )
option(NCIP_BACKEND_PORTFOLIO       "Build NCIP with parallel portfolio approach."             ON )

set(NCIP_WRAPPER_VERSION   "HWMCC25" CACHE STRING "Version of NCIP wrapper for the HWMCC."        )

#--------------------------------------------------------------------------------------------------
# Library version:

set(NCIP_SOMAJOR   0)
set(NCIP_SOMINOR   1)
set(NCIP_SORELEASE 0)

if(USE_SORELEASE)
  set(NCIP_VERSION ${NCIP_SOMAJOR}.${NCIP_SOMINOR}.${NCIP_SORELEASE})
else()
  set(NCIP_VERSION ${NCIP_SOMAJOR}.${NCIP_SOMINOR})
endif()
set(NCIP_SOVERSION ${NCIP_SOMAJOR})

#--------------------------------------------------------------------------------------------------
# Compile flags:

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)

#--------------------------------------------------------------------------------------------------
# Library Targets:

include(deps/aiger.cmake)
add_subdirectory(deps/cadicraig)
add_subdirectory(deps/kittencraig)
add_subdirectory(deps/minicraig)

set(NCIP_LIB_SOURCES src/bmc-ncip.cpp src/bmc-problem.cpp
  src/bmc-format-aig.cpp src/bmc-format-cip.cpp src/bmc-format-dimspec.cpp
  src/bmc-io-aig.cpp src/bmc-io-cip.cpp src/bmc-io-dimspec.cpp)
set(NCIP_LIB_HEADERS src/bmc-ncip.hpp src/bmc-problem.hpp src/bmc-ncip-portfolio.hpp
  src/bmc-format-aig.hpp src/bmc-format-cip.hpp src/bmc-format-dimspec.hpp
  src/bmc-io-aig.hpp src/bmc-io-cip.hpp src/bmc-io-dimspec.hpp)
add_library(ncip-lib-static STATIC ${NCIP_LIB_SOURCES})
add_library(ncip-lib-shared SHARED ${NCIP_LIB_SOURCES})
target_include_directories(ncip-lib-static PUBLIC src)
target_include_directories(ncip-lib-shared PUBLIC src)
target_link_libraries(ncip-lib-static aiger-lib-static)
target_link_libraries(ncip-lib-shared aiger-lib-static)
set_target_properties(ncip-lib-static PROPERTIES
  OUTPUT_NAME "ncip"
  PUBLIC_HEADER "${NCIP_LIB_HEADERS}")
set_target_properties(ncip-lib-shared PROPERTIES
  OUTPUT_NAME "ncip"
  PUBLIC_HEADER "${NCIP_LIB_HEADERS}"
  VERSION ${NCIP_VERSION}
  SOVERSION ${NCIP_SOVERSION})
install(TARGETS ncip-lib-static ncip-lib-shared
  PUBLIC_HEADER DESTINATION include/ncip)

#--------------------------------------------------------------------------------------------------
# Executable Targets:

if(FULLY_STATIC_BINARIES)
  set(NCIP_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(NCIP_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(NCIP_LIB_LINK "static")
else(STATIC_BINARIES)
  set(NCIP_LIB_LINK "shared")
endif(STATIC_BINARIES)

function(create_alias executable format)
  set(NCIP_EXECUTABLE ${executable})
  set(NCIP_FORMAT ${format})

if(NCIP_WRAPPER_VERSION STREQUAL HWMCC24)
  configure_file(util/wrapper-hwmcc24.in
    ncip-${executable}-${format}
    FILE_PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_WRITE GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
    @ONLY)
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/ncip-${executable}-${format}
    TYPE BIN)
elseif(NCIP_WRAPPER_VERSION STREQUAL HWMCC25)
  configure_file(util/wrapper-hwmcc25.in
    ncip-${executable}-${format}.cpp
    @ONLY)
  add_executable(ncip-${executable}-${format}
    ${CMAKE_CURRENT_BINARY_DIR}/ncip-${executable}-${format}.cpp)
  set_target_properties(ncip-${executable}-${format} PROPERTIES OUTPUT_NAME "ncip-${executable}-${format}")
  install(TARGETS ncip-${executable}-${format})
endif()

  unset(NCIP_EXECUTABLE)
  unset(NCIP_FORMAT)
endfunction(create_alias)

if(NCIP_BACKEND_MINICRAIG)
  target_link_libraries(ncip-lib-static minicraig-lib-static)
  target_link_libraries(ncip-lib-shared minicraig-lib-shared)
  target_compile_definitions(ncip-lib-static PUBLIC NCIP_BACKEND_MINICRAIG=YES)
  target_compile_definitions(ncip-lib-shared PUBLIC NCIP_BACKEND_MINICRAIG=YES)

  add_executable(ncip-minicraig src/bmc-prover.cpp)
  target_compile_definitions(ncip-minicraig PRIVATE NCIP_SOLVER_MINICRAIG=YES)
  target_link_libraries(ncip-minicraig ncip-lib-${NCIP_LIB_LINK})
  target_link_options(ncip-minicraig PRIVATE ${NCIP_BIN_LINK})
  set_target_properties(ncip-minicraig PROPERTIES OUTPUT_NAME "ncip-minicraig")
  install(TARGETS ncip-minicraig)

  create_alias(minicraig aiger)
  create_alias(minicraig dimspec)
  create_alias(minicraig cip)
endif(NCIP_BACKEND_MINICRAIG)

if(NCIP_BACKEND_MINICRAIG_DEBUG)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_link_libraries(ncip-lib-static minicraig-lib-static)
  target_link_libraries(ncip-lib-shared minicraig-lib-shared)
  target_compile_definitions(ncip-lib-static PUBLIC NCIP_BACKEND_MINICRAIG=YES)
  target_compile_definitions(ncip-lib-shared PUBLIC NCIP_BACKEND_MINICRAIG=YES)

  add_executable(ncip-minicraig-debug src/bmc-prover.cpp)
  target_compile_definitions(ncip-minicraig-debug PRIVATE NCIP_SOLVER_MINICRAIG_DEBUG=YES)
  target_link_libraries(ncip-minicraig-debug ncip-lib-${NCIP_LIB_LINK})
  target_link_options(ncip-minicraig-debug PRIVATE ${NCIP_BIN_LINK})
  set_target_properties(ncip-minicraig-debug PROPERTIES OUTPUT_NAME "ncip-minicraig-debug")
  install(TARGETS ncip-minicraig-debug)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(WARNING "ncip-minicraig-debug is only built in Debug mode")
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")
endif(NCIP_BACKEND_MINICRAIG_DEBUG)

if(NCIP_BACKEND_CADICRAIG)
  target_link_libraries(ncip-lib-static cadicraig-lib-static)
  target_link_libraries(ncip-lib-shared cadicraig-lib-shared)
  target_compile_definitions(ncip-lib-static PUBLIC NCIP_BACKEND_CADICRAIG=YES)
  target_compile_definitions(ncip-lib-shared PUBLIC NCIP_BACKEND_CADICRAIG=YES)

  add_executable(ncip-cadicraig src/bmc-prover.cpp)
  target_compile_definitions(ncip-cadicraig PRIVATE NCIP_SOLVER_CADICRAIG=YES)
  target_link_libraries(ncip-cadicraig ncip-lib-${NCIP_LIB_LINK})
  target_link_options(ncip-cadicraig PRIVATE ${NCIP_BIN_LINK})
  set_target_properties(ncip-cadicraig PROPERTIES OUTPUT_NAME "ncip-cadicraig")
  install(TARGETS ncip-cadicraig)

  create_alias(cadicraig aiger)
  create_alias(cadicraig dimspec)
  create_alias(cadicraig cip)
endif(NCIP_BACKEND_CADICRAIG)

if(NCIP_BACKEND_CADICRAIG_DEBUG)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_link_libraries(ncip-lib-static cadicraig-lib-static)
  target_link_libraries(ncip-lib-shared cadicraig-lib-shared)
  target_compile_definitions(ncip-lib-static PUBLIC NCIP_BACKEND_CADICRAIG=YES)
  target_compile_definitions(ncip-lib-shared PUBLIC NCIP_BACKEND_CADICRAIG=YES)

  add_executable(ncip-cadicraig-debug src/bmc-prover.cpp)
  target_compile_definitions(ncip-cadicraig-debug PRIVATE NCIP_SOLVER_CADICRAIG_DEBUG=YES)
  target_link_libraries(ncip-cadicraig-debug ncip-lib-${NCIP_LIB_LINK})
  target_link_options(ncip-cadicraig-debug PRIVATE ${NCIP_BIN_LINK})
  set_target_properties(ncip-cadicraig-debug PROPERTIES OUTPUT_NAME "ncip-cadicraig-debug")
  install(TARGETS ncip-cadicraig-debug)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(WARNING "ncip-cadicraig-debug is only built in Debug mode")
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")
endif(NCIP_BACKEND_CADICRAIG_DEBUG)

if(NCIP_BACKEND_KITTENCRAIG)
  target_link_libraries(ncip-lib-static kittencraig-lib-static)
  target_link_libraries(ncip-lib-shared kittencraig-lib-shared)
  target_compile_definitions(ncip-lib-static PUBLIC NCIP_BACKEND_KITTENCRAIG=YES)
  target_compile_definitions(ncip-lib-shared PUBLIC NCIP_BACKEND_KITTENCRAIG=YES)

  add_executable(ncip-kittencraig src/bmc-prover.cpp)
  target_compile_definitions(ncip-kittencraig PRIVATE NCIP_SOLVER_KITTENCRAIG=YES)
  target_link_libraries(ncip-kittencraig ncip-lib-${NCIP_LIB_LINK})
  target_link_options(ncip-kittencraig PRIVATE ${NCIP_BIN_LINK})
  set_target_properties(ncip-kittencraig PROPERTIES OUTPUT_NAME "ncip-kittencraig")
  install(TARGETS ncip-kittencraig)

  create_alias(kittencraig aiger)
  create_alias(kittencraig dimspec)
  create_alias(kittencraig cip)
endif(NCIP_BACKEND_KITTENCRAIG)

if(NCIP_BACKEND_PORTFOLIO)
  add_executable(ncip-portfolio src/bmc-prover.cpp)
  target_compile_definitions(ncip-portfolio PRIVATE NCIP_SOLVER_PORTFOLIO=YES)
  target_link_libraries(ncip-portfolio ncip-lib-${NCIP_LIB_LINK})
  target_link_options(ncip-portfolio PRIVATE ${NCIP_BIN_LINK})
  set_target_properties(ncip-portfolio PROPERTIES OUTPUT_NAME "ncip-portfolio")
  install(TARGETS ncip-portfolio)

  create_alias(portfolio aiger)
  create_alias(portfolio dimspec)
  create_alias(portfolio cip)
endif(NCIP_BACKEND_PORTFOLIO)