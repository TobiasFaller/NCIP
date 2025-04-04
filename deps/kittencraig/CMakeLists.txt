cmake_minimum_required(VERSION 3.22.1)

project(kittencraig)

find_package(ZLIB)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(LTO                   "Enable Link-Time Optimization"                            ON )
option(STATIC_BINARIES       "Link binaries statically."                                ON )
option(FULLY_STATIC_BINARIES "Link binaries fully statically (including libc++, etc.)." ON )
option(USE_SORELEASE         "Use SORELEASE in shared library filename."                ON )
option(KITTENCRAIG_LOGGING   "Enable logging output"                                    OFF)

#--------------------------------------------------------------------------------------------------
# Library version:

set(KITTENCRAIG_SOMAJOR   0)
set(KITTENCRAIG_SOMINOR   1)
set(KITTENCRAIG_SORELEASE 0)

# Compute VERSION and SOVERSION:
if (USE_SORELEASE)
  set(KITTENCRAIG_VERSION ${KITTENCRAIG_SOMAJOR}.${KITTENCRAIG_SOMINOR}.${KITTENCRAIG_SORELEASE})
else()
  set(KITTENCRAIG_VERSION ${KITTENCRAIG_SOMAJOR}.${KITTENCRAIG_SOMINOR})
endif()
set(KITTENCRAIG_SOVERSION ${KITTENCRAIG_SOMAJOR})

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

set(KITTEN_DIR ${PROJECT_SOURCE_DIR}/../kissat)
set(KITTEN_LIB_SOURCES
  ${KITTEN_DIR}/src/kitten.c)
set(KITTEN_LIB_HEADERS
  ${KITTEN_DIR}/src/kitten.h)
add_library(kitten-lib-static STATIC ${KITTEN_LIB_SOURCES})
add_library(kitten-lib-shared SHARED ${KITTEN_LIB_SOURCES})
target_compile_definitions(kitten-lib-static PUBLIC LIBRARY_KITTEN=YES)
target_compile_definitions(kitten-lib-shared PUBLIC LIBRARY_KITTEN=YES)
target_include_directories(kitten-lib-static PUBLIC ${KITTEN_DIR}/src)
target_include_directories(kitten-lib-shared PUBLIC ${KITTEN_DIR}/src)
set_target_properties(kitten-lib-static PROPERTIES
  OUTPUT_NAME "kitten"
  PUBLIC_HEADER "${KITTEN_LIB_HEADERS}")
set_target_properties(kitten-lib-shared PROPERTIES
  OUTPUT_NAME "kitten"
  VERSION ${KITTENCRAIG_VERSION}
  SOVERSION ${KITTENCRAIG_SOVERSION}
  PUBLIC_HEADER "${KITTEN_LIB_HEADERS}")
install(TARGETS kitten-lib-static kitten-lib-shared
  PUBLIC_HEADER DESTINATION include/kitten)

add_library(ipasirkitten-lib-static STATIC ${KITTEN_LIB_SOURCES})
add_library(ipasirkitten-lib-shared SHARED ${KITTEN_LIB_SOURCES})
target_compile_definitions(ipasirkitten-lib-static PUBLIC LIBRARY_KITTEN=YES IPASIR_KITTEN=YES)
target_compile_definitions(ipasirkitten-lib-shared PUBLIC LIBRARY_KITTEN=YES IPASIR_KITTEN=YES)
target_include_directories(ipasirkitten-lib-static PUBLIC ${KITTEN_DIR}/src)
target_include_directories(ipasirkitten-lib-shared PUBLIC ${KITTEN_DIR}/src)
set_target_properties(ipasirkitten-lib-static PROPERTIES
  OUTPUT_NAME "ipasirkitten"
  PUBLIC_HEADER "${KITTEN_LIB_HEADERS}")
set_target_properties(ipasirkitten-lib-shared PROPERTIES
  OUTPUT_NAME "ipasirkitten"
  VERSION ${KITTENCRAIG_VERSION}
  SOVERSION ${KITTENCRAIG_SOVERSION}
  PUBLIC_HEADER "${KITTEN_LIB_HEADERS}")
install(TARGETS ipasirkitten-lib-static ipasirkitten-lib-shared
  PUBLIC_HEADER DESTINATION include/kitten)

#--------------------------------------------------------------------------------------------------

set(KITTENCRAIG_LIB_SOURCES
  src/kittentracer.cpp)
set(KITTENCRAIG_LIB_HEADERS
  src/kittentracer.hpp)
add_library(kittencraig-lib-static STATIC ${KITTENCRAIG_LIB_SOURCES})
add_library(kittencraig-lib-shared SHARED ${KITTENCRAIG_LIB_SOURCES})
target_include_directories(kittencraig-lib-static PUBLIC src)
target_include_directories(kittencraig-lib-shared PUBLIC src)
target_link_libraries(kittencraig-lib-static kitten-lib-static)
target_link_libraries(kittencraig-lib-shared kitten-lib-shared)
set_target_properties(kittencraig-lib-static PROPERTIES
  OUTPUT_NAME "kittencraig"
  PUBLIC_HEADER "${KITTENCRAIG_LIB_HEADERS}")
set_target_properties(kittencraig-lib-shared PROPERTIES
  OUTPUT_NAME "kittencraig"
  VERSION ${KITTENCRAIG_VERSION}
  SOVERSION ${KITTENCRAIG_SOVERSION}
  PUBLIC_HEADER "${KITTENCRAIG_LIB_HEADERS}")
install(TARGETS kittencraig-lib-static kittencraig-lib-shared
  PUBLIC_HEADER DESTINATION include/kittencraig)

if(KITTENCRAIG_LOGGING)
  target_compile_definitions(kittencraig-lib-static PRIVATE LOGGING)
  target_compile_definitions(kittencraig-lib-shared PRIVATE LOGGING)
endif(KITTENCRAIG_LOGGING)

#--------------------------------------------------------------------------------------------------
# Executable Targets:

if(FULLY_STATIC_BINARIES)
  set(KITTENCRAIG_BIN_LINK "-static")
  set(STATIC_BINARIES ON)
else(FULLY_STATIC_BINARIES)
  set(KITTENCRAIG_BIN_LINK "")
endif(FULLY_STATIC_BINARIES)

if(STATIC_BINARIES)
  set(KITTENCRAIG_LIB_LINK "static")
else(STATIC_BINARIES)
  set(KITTENCRAIG_LIB_LINK "shared")
endif(STATIC_BINARIES)

add_executable(kittencraig src/kittencraig.cpp)
target_include_directories(kittencraig PRIVATE ${ZLIB_INCLUDE_DIR})
target_link_libraries(kittencraig kitten-lib-${KITTENCRAIG_LIB_LINK} kittencraig-lib-${KITTENCRAIG_LIB_LINK} z)
target_link_options(kittencraig PRIVATE ${KITTENCRAIG_BIN_LINK})
set_target_properties(kittencraig PROPERTIES OUTPUT_NAME "kittencraig")
install(TARGETS kittencraig)
