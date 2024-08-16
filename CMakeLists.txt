# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)
project(aws-greengrass-lite C)

#
# Options
#

option(BUILD_TESTING "Build tests")
option(BUILD_EXAMPLES "Build examples" TRUE)

option(ENABLE_WERROR "Compile warnings as errors")

set(GGL_LOG_LEVEL CACHE STRING "GGL log level")

option(GGL_IPC_AUTH_DISABLE "Put IPC authentication in debug mode")

#
# Misc
#

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(BUILD_TESTING)
  include(CTest)
endif()

include(GNUInstallDirs)

# Put outputs in build/bin and build/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

# Fix for CMake stripping pkg-config includes from compile command
unset(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES)

#
# Compiler options
#

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(WARNING "CMAKE_BUILD_TYPE not set, using MinSizeRel.")
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

# Enable a compiler option for compile and link
macro(add_cflags)
  add_compile_options(${ARGN})
  add_link_options(${ARGN})
endmacro()

include(CheckCCompilerFlag)
include(CheckLinkerFlag)

# Enable a compiler/linker option if supported with a condition
macro(try_add_cflag_if name cond option)
  check_c_compiler_flag("${option}" compiler_has_${name})
  if(compiler_has_${name})
    add_cflags("$<${cond}:${option}>")
  endif()
endmacro()

# Enable a compiler/linker option if supported
macro(try_add_cflag name option)
  try_add_cflag_if(${name} 1 ${option})
endmacro()

# Enable a linker option if supported with a condition
macro(try_add_link_option_if name cond option)
  check_linker_flag(C "${option}" linker_has_${name})
  if(linker_has_${name})
    add_link_options("$<${cond}:${option}>")
  endif()
endmacro()

# Enable a linker option if supported
macro(try_add_link_option name option)
  try_add_link_option_if(${name} 1 ${option})
endmacro()

# Clear CMake defaults
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_MINSIZEREL "")

set(CMAKE_COLOR_DIAGNOSTICS ON)

add_cflags($<$<CONFIG:Debug>:-O0>)
add_cflags($<$<CONFIG:Release,RelWithDebInfo>:-O3>)
try_add_cflag_if(Oz $<CONFIG:MinSizeRel> -Oz)
if(NOT compiler_has_Oz)
  add_cflags($<$<CONFIG:MinSizeRel>:-Os>)
endif()

add_cflags($<$<CONFIG:Debug,RelWithDebInfo>:-ggdb3>)

add_cflags($<$<NOT:$<CONFIG:Debug>>:-fPIE>)

if(ENABLE_WERROR)
  add_cflags(-Werror)
endif()

add_cflags(
  -pthread
  -std=gnu11
  -Wall
  -Wextra
  -Wvla
  -Wshadow
  -Wformat=2
  -Wmissing-prototypes
  -Wstrict-prototypes
  -Wold-style-definition
  -Wunused
  -Wundef
  -Wconversion
  -Wredundant-decls
  -Wdate-time
  -Wstack-protector
  -fvisibility=hidden
  -fno-semantic-interposition
  -fstack-protector-strong)

try_add_cflag(zero-call-used-regs -fzero-call-used-regs=used-gpr)
try_add_cflag(stack-clash-protection -fstack-clash-protection)

add_cflags($<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
try_add_cflag_if(var-init-pattern $<CONFIG:Debug>
                 -ftrivial-auto-var-init=pattern)

add_cflags($<$<CONFIG:Debug>:-fsanitize=undefined,address>)

try_add_cflag_if(function-sections $<NOT:$<CONFIG:Debug>> -ffunction-sections)
try_add_cflag_if(data-sections $<NOT:$<CONFIG:Debug>> -fdata-sections)

add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)
if(GGL_IPC_AUTH_DISABLE)
  add_compile_definitions(GGL_IPC_AUTH_DISABLE)
endif()

add_link_options($<$<NOT:$<CONFIG:Debug>>:-pie>)

add_link_options(LINKER:-z,relro,-z,now,-z,noexecstack LINKER:--as-needed
                 LINKER:--enable-new-dtags,--hash-style=gnu)
try_add_link_option(compress-debug-sections-zlib
                    LINKER:--compress-debug-sections=zlib)

try_add_link_option_if(gc-sections $<NOT:$<CONFIG:Debug>> LINKER:--gc-sections)
try_add_link_option_if(strip-all $<CONFIG:Release,MinSizeRel> LINKER:-s)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> rcs --thin <TARGET> <OBJECTS>")
set(CMAKE_C_CREATE_STATIC_LIBRARY_IPO
    "\"${CMAKE_C_COMPILER_AR}\" rcs --thin <TARGET> <OBJECTS>")

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

#
# FetchContent deps
#

set(FETCHCONTENT_QUIET FALSE)
include(FetchContent)

file(READ fc_deps.json FC_DEPS_JSON)
string(JSON FC_DEPS_COUNT LENGTH "${FC_DEPS_JSON}")
math(EXPR FC_DEPS_INDEX_MAX "${FC_DEPS_COUNT} - 1")

foreach(index RANGE ${FC_DEPS_INDEX_MAX})
  string(JSON dep_name MEMBER "${FC_DEPS_JSON}" ${index})
  string(JSON dep_url GET "${FC_DEPS_JSON}" "${dep_name}" url)
  string(JSON dep_rev GET "${FC_DEPS_JSON}" "${dep_name}" rev)
  fetchcontent_declare(
    "${dep_name}"
    GIT_REPOSITORY "${dep_url}"
    GIT_TAG "${dep_rev}")
  string(TOLOWER "${dep_name}" dep_name_lower)
  fetchcontent_getproperties(${dep_name})
  if(NOT ${dep_name_lower}_POPULATED)
    fetchcontent_populate(${dep_name})
  endif()
endforeach()

#
# System deps
#

find_package(PkgConfig REQUIRED)

pkg_search_module(openssl REQUIRED IMPORTED_TARGET openssl)
pkg_search_module(libcurl REQUIRED IMPORTED_TARGET libcurl)
pkg_search_module(sqlite3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_search_module(yaml REQUIRED IMPORTED_TARGET yaml-0.1)
pkg_search_module(libsystemd REQUIRED IMPORTED_TARGET libsystemd)
pkg_search_module(uuid REQUIRED IMPORTED_TARGET uuid)

#
# Add components
#

# Common setup for a GGL module
function(ggl_init_module name)
  cmake_parse_arguments(PARSE_ARGV 1 COMP_ARG "" "SRCDIR" "INCDIRS;LIBS")
  if("${COMP_ARG_SRCDIR}" STREQUAL "")
    set(COMP_ARG_SRCDIR src)
  endif()
  if("${COMP_ARG_INCDIRS}" STREQUAL "")
    set(COMP_ARG_INCDIRS include)
  endif()

  file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS "${COMP_ARG_SRCDIR}/*.c")
  list(LENGTH SRCS SRCS_LEN)

  if(NOT SRCS_LEN EQUAL 0)
    add_library(${name} STATIC ${SRCS})
    target_include_directories(${name} PRIVATE ${COMP_ARG_INCDIRS})
    target_include_directories(${name} SYSTEM INTERFACE ${COMP_ARG_INCDIRS})
    target_link_libraries(${name} PRIVATE ${COMP_ARG_LIBS})
  endif()

  file(GLOB BINS CONFIGURE_DEPENDS "bin/*.c")
  foreach(bin ${BINS})
    get_filename_component(bin_name ${bin} NAME_WE)
    add_executable(${bin_name}-bin ${bin})
    if(NOT SRCS_LEN EQUAL 0)
      target_link_libraries(${bin_name}-bin PRIVATE ${name})
    endif()
    target_link_libraries(${bin_name}-bin PRIVATE ${COMP_ARG_LIBS})
    set_target_properties(${bin_name}-bin PROPERTIES OUTPUT_NAME ${bin_name})
    install(TARGETS ${bin_name}-bin)
  endforeach()
endfunction()

add_subdirectory(ggl-lib)
add_subdirectory(ggl-http)
add_subdirectory(ggl-socket)
add_subdirectory(ggl-file)
add_subdirectory(eventstream)
add_subdirectory(ggl-json)
add_subdirectory(ggl-yaml)
add_subdirectory(core-bus)
add_subdirectory(ggipcd)
add_subdirectory(ggipc-auth)
add_subdirectory(ggipc-client)
add_subdirectory(ggconfigd)
add_subdirectory(gghealthd)
add_subdirectory(deps/core_mqtt)
add_subdirectory(iotcored)
add_subdirectory(ggpubsubd)
add_subdirectory(ggdeploymentd)
add_subdirectory(gg-fleet-statusd)
add_subdirectory(tesd)
add_subdirectory(fleet-provisioning)
add_subdirectory(ggl-config-init)
add_subdirectory(recipe-runner)

if(BUILD_EXAMPLES)
  add_subdirectory(samples/echo-server)
  add_subdirectory(samples/example-client)
  add_subdirectory(samples/mqtt-client)
  add_subdirectory(samples/deployment-client)
  add_subdirectory(samples/status-monitor-client)
  add_subdirectory(configtest)
  add_subdirectory(tesd-test)

  add_custom_target(
    run_nucleus ALL
    COMMAND cp "${CMAKE_SOURCE_DIR}/misc/run_nucleus" "${CMAKE_BINARY_DIR}/bin"
    COMMENT "Nucleus run script for testing")
endif()
