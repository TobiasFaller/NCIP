cmake_minimum_required(VERSION 3.22.1)

project(minicraig)

find_package(ZLIB)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(LTO                       "Enable Link-Time Optimization"                            ON )
option(STATIC_BINARIES           "Link binaries statically."                                ON )
option(FULLY_STATIC_BINARIES     "Link binaries fully statically (including libc++, etc.)." ON )
option(USE_SORELEASE             "Use SORELEASE in shared library filename."                ON )
option(MINICRAIG_DEBUG_CRAIG     "Enable debugging of Craig interpolation"                  OFF)
option(MINICRAIG_DEBUG_CRAIG_ALL "Enable full debugging of Craig interpolation"             OFF)
option(MINICRAIG_DEBUG_PREPROC   "Enable debugging of preprocessing"                        OFF)
option(MINICRAIG_FUZZ_CRAIG      "Enable fuzzing of Craig interpolation"                    OFF)
option(MINICRAIG_OFFSET_REUSE    "Enable reusing AIG tseitin literal ids"                   OFF)
#--------------------------------------------------------------------------------------------------
# Library version:

set(MINICRAIG_SOMAJOR   2)
set(MINICRAIG_SOMINOR   1)
set(MINICRAIG_SORELEASE 0)

# Compute VERSION and SOVERSION:
if (USE_SORELEASE)
  set(MINICRAIG_VERSION ${MINICRAIG_SOMAJOR}.${MINICRAIG_SOMINOR}.${MINICRAIG_SORELEASE})
else()
  set(MINICRAIG_VERSION ${MINICRAIG_SOMAJOR}.${MINICRAIG_SOMINOR})
endif()
set(MINICRAIG_SOVERSION ${MINICRAIG_SOMAJOR})

#--------------------------------------------------------------------------------------------------
# Compile flags:

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)

if(MINICRAIG_DEBUG_CRAIG)
  add_compile_definitions(CRAIG_INTERPOLATION_DEBUG)
endif(MINICRAIG_DEBUG_CRAIG)

if(MINICRAIG_DEBUG_CRAIG_ALL)
  add_compile_definitions(CRAIG_INTERPOLATION_DEBUG_SOLVE)
endif(MINICRAIG_DEBUG_CRAIG_ALL)

if(MINICRAIG_DEBUG_PREPROC)
  add_compile_definitions(PREPROCESSOR_DEBUG)
endif(MINICRAIG_DEBUG_PREPROC)

if(MINICRAIG_FUZZ_CRAIG)
  add_compile_definitions(CRAIG_INTERPOLATION_FUZZ_CRAIG)
endif(MINICRAIG_FUZZ_CRAIG)

if(MINICRAIG_OFFSET_REUSE)
  add_compile_definitions(WITH_OFFSET_REUSE)
endif(MINICRAIG_OFFSET_REUSE)

#--------------------------------------------------------------------------------------------------
# Library Targets:

set(MINICRAIG_LIB_SOURCES
  minisat/mtl/Aig.cc
  minisat/utils/Options.cc
  minisat/utils/System.cc
  minisat/core/Solver.cc
  minisat/simp/SimpSolver.cc
  minisat/preproc/Preprocessor.cc
  minisat/craig/CraigTypes.cc
  minisat/craig/CraigSolver.cc
  minisat/craig/CraigSimpSolver.cc)
set(MINICRAIG_LIB_HEADER_DIRS
  minisat/mtl
  minisat/craig
  minisat/utils
  minisat/preproc
  minisat/simp
  minisat/core)
add_library(minicraig-lib-static STATIC ${MINICRAIG_LIB_SOURCES})
add_library(minicraig-lib-shared SHARED ${MINICRAIG_LIB_SOURCES})
target_include_directories(minicraig-lib-static PRIVATE ${ZLIB_INCLUDE_DIR} PUBLIC minisat)
target_include_directories(minicraig-lib-shared PRIVATE ${ZLIB_INCLUDE_DIR} PUBLIC minisat)
target_link_libraries(minicraig-lib-shared z)
target_link_libraries(minicraig-lib-static z)
set_target_properties(minicraig-lib-static PROPERTIES
  OUTPUT_NAME "minicraig")
set_target_properties(minicraig-lib-shared PROPERTIES
  OUTPUT_NAME "minicraig"
  VERSION ${MINICRAIG_VERSION}
  SOVERSION ${MINICRAIG_SOVERSION})
install(TARGETS minicraig-lib-static minicraig-lib-shared)
install(DIRECTORY ${MINICRAIG_LIB_HEADER_DIRS}
  DESTINATION include/minicraig
  FILES_MATCHING PATTERN "*.h")

#--------------------------------------------------------------------------------------------------
# Executable Targets:

if(FULLY_STATIC_BINARIES)
  set(MINICRAIG_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(MINICRAIG_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(MINICRAIG_LIB_LINK "static")
else(STATIC_BINARIES)
  set(MINICRAIG_LIB_LINK "shared")
endif(STATIC_BINARIES)

add_executable(minicraig-core minisat/core/Main.cc)
target_compile_options(minicraig-core PRIVATE -Wno-alloc-size-larger-than)
target_link_libraries(minicraig-core minicraig-lib-${MINICRAIG_LIB_LINK})
target_link_options(minicraig-core PRIVATE ${MINICRAIG_BIN_LINK})
set_target_properties(minicraig-core PROPERTIES OUTPUT_NAME "minicraig-core")
install(TARGETS minicraig-core)

add_executable(minicraig-simp minisat/simp/Main.cc)
target_compile_options(minicraig-simp PRIVATE -Wno-alloc-size-larger-than)
target_link_libraries(minicraig-simp minicraig-lib-${MINICRAIG_LIB_LINK})
target_link_options(minicraig-simp PRIVATE ${MINICRAIG_BIN_LINK})
set_target_properties(minicraig-simp PROPERTIES OUTPUT_NAME "minicraig-simp")
install(TARGETS minicraig-simp)

add_executable(minicraig-preproc minisat/preproc/Main.cc)
target_compile_options(minicraig-preproc PRIVATE -Wno-alloc-size-larger-than)
target_link_libraries(minicraig-preproc minicraig-lib-${MINICRAIG_LIB_LINK})
target_link_options(minicraig-preproc PRIVATE ${MINICRAIG_BIN_LINK})
set_target_properties(minicraig-preproc PROPERTIES OUTPUT_NAME "minicraig-preproc")
install(TARGETS minicraig-preproc)

add_executable(minicraig-craig minisat/craig/Main.cc)
target_compile_definitions(minicraig-craig PUBLIC DISABLE_SIMP)
target_compile_options(minicraig-craig PRIVATE -Wno-alloc-size-larger-than)
target_link_libraries(minicraig-craig minicraig-lib-${MINICRAIG_LIB_LINK})
target_link_options(minicraig-craig PRIVATE ${MINICRAIG_BIN_LINK})
set_target_properties(minicraig-craig PROPERTIES OUTPUT_NAME "minicraig-craig")
install(TARGETS minicraig-craig)

add_executable(minicraig-simpcraig minisat/craig/Main.cc)
target_compile_definitions(minicraig-simpcraig PUBLIC ENABLE_SIMP)
target_compile_options(minicraig-simpcraig PRIVATE -Wno-alloc-size-larger-than)
target_link_libraries(minicraig-simpcraig minicraig-lib-${MINICRAIG_LIB_LINK})
target_link_options(minicraig-simpcraig PRIVATE ${MINICRAIG_BIN_LINK})
set_target_properties(minicraig-simpcraig PROPERTIES OUTPUT_NAME "minicraig-simpcraig")
install(TARGETS minicraig-simpcraig)
