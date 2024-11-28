# aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)
project(aws-greengrass-lite C ASM)

#
# Options
#

option(BUILD_TESTING "Build tests")
option(BUILD_EXAMPLES "Build examples" TRUE)

option(ENABLE_WERROR "Compile warnings as errors")

set(GGL_LOG_LEVEL CACHE STRING "GGL log level")

option(GGL_IPC_AUTH_DISABLE "Put IPC authentication in debug mode")

option(GGL_SYSTEMD_SERVICE_BUILD "Install GGL as a set of systemd services"
       TRUE)
set(GGL_SYSTEMD_SYSTEM_DIR
    "lib/systemd/system"
    CACHE STRING "Install directory for systemd unit files")
set(GGL_SYSTEMD_SYSTEM_USER
    "ggcore"
    CACHE STRING "Core service user")
set(GGL_SYSTEMD_SYSTEM_GROUP
    "ggcore"
    CACHE STRING "Core service group")

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

# Following flags are required
add_cflags(-pthread -fno-strict-aliasing)

# Following flags are optional
add_cflags(-std=gnu11 -fvisibility=hidden -fno-semantic-interposition
           -fno-common)

try_add_cflag(stack-protector-strong -fstack-protector-strong)
try_add_cflag(strict-flex-arrays -fstrict-flex-arrays=3)
try_add_cflag(zero-call-used-regs -fzero-call-used-regs=used-gpr)
try_add_cflag(stack-clash-protection -fstack-clash-protection)
try_add_cflag(cf-protection -fcf-protection=full)
try_add_cflag(branch-protection -mbranch-protection=standard)

try_add_cflag(macro-prefix-map "-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}/=")

add_cflags($<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
try_add_cflag_if(trivial-auto-var-init-pattern $<CONFIG:Debug>
                 -ftrivial-auto-var-init=pattern)

add_cflags($<$<CONFIG:Debug>:-fsanitize=undefined,address>)

add_cflags($<$<NOT:$<CONFIG:Debug>>:-fno-delete-null-pointer-checks>)
add_cflags($<$<NOT:$<CONFIG:Debug>>:-fwrapv>)
try_add_cflag_if(trivial-auto-var-init-zero $<NOT:$<CONFIG:Debug>>
                 -ftrivial-auto-var-init=zero)

try_add_cflag_if(function-sections $<NOT:$<CONFIG:Debug>> -ffunction-sections)
try_add_cflag_if(data-sections $<NOT:$<CONFIG:Debug>> -fdata-sections)

if(ENABLE_WERROR)
  add_cflags(-Werror)
endif()

add_cflags(
  -Wall
  -Wextra
  -Wvla
  -Wshadow
  -Wformat
  -Wformat=2
  -Wmissing-prototypes
  -Wstrict-prototypes
  -Wold-style-definition
  -Wunused
  -Wundef
  -Wconversion
  -Wsign-conversion
  -Wimplicit-fallthrough
  -Wredundant-decls
  -Wdate-time
  -Wstack-protector)

try_add_cflag(Wenum-int-mismatch -Wenum-int-mismatch)
try_add_cflag(Wtrampolines -Wtrampolines)
try_add_cflag(Wbidi-chars -Wbidi-chars=any,ucn)

add_cflags(-Werror=format-security -Werror=implicit
           -Werror=incompatible-pointer-types -Werror=int-conversion)

add_compile_definitions(_GNU_SOURCE)
# build ggl-http with obsoleted cURL code removed
add_compile_definitions(CURL_NO_OLDIES)

add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>)

if(GGL_IPC_AUTH_DISABLE)
  add_compile_definitions(GGL_IPC_AUTH_DISABLE)
endif()

add_link_options($<$<NOT:$<CONFIG:Debug>>:-pie>)

add_link_options(
  LINKER:-z,relro,-z,now,-z,noexecstack,-z,nodlopen LINKER:--as-needed
  LINKER:--no-copy-dt-needed-entries LINKER:--enable-new-dtags,--hash-style=gnu)
try_add_link_option(compress-debug-sections-zlib
                    LINKER:--compress-debug-sections=zlib)

# TODO: investigate misaligned pointers when this is enabled for 32-bit arm
# try_add_link_option_if(gc-sections $<NOT:$<CONFIG:Debug>>
# LINKER:--gc-sections)

try_add_link_option_if(strip-all $<CONFIG:Release,MinSizeRel> LINKER:-s)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

if(NOT DEFINED ar_thin_flag)
  execute_process(COMMAND sh -c "${CMAKE_AR} --help | grep -- --thin"
                  RESULT_VARIABLE ar_has_thin)
  if(ar_has_thin EQUAL 0)
    set(ar_thin_flag
        "--thin"
        CACHE INTERNAL "")
  else()
    set(ar_thin_flag
        ""
        CACHE INTERNAL "")
  endif()
endif()

set(CMAKE_C_CREATE_STATIC_LIBRARY
    "<CMAKE_AR> rcs ${ar_thin_flag} <TARGET> <OBJECTS>")
set(CMAKE_C_CREATE_STATIC_LIBRARY_IPO
    "\"${CMAKE_C_COMPILER_AR}\" rcs ${ar_thin_flag} <TARGET> <OBJECTS>")

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

set(ENV{SOURCE_DATE_EPOCH} "0")
set(ENV{ZERO_AR_DATE} "1")

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
    GIT_TAG "${dep_rev}"
    SOURCE_SUBDIR nonexistent_dir)
  fetchcontent_makeavailable(${dep_name})
endforeach()

#
# System deps
#

find_package(PkgConfig REQUIRED)

pkg_search_module(openssl REQUIRED IMPORTED_TARGET openssl)
pkg_search_module(libcurl REQUIRED IMPORTED_TARGET libcurl>=7.86)
pkg_search_module(sqlite3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_search_module(yaml REQUIRED IMPORTED_TARGET yaml-0.1)
pkg_search_module(libsystemd REQUIRED IMPORTED_TARGET libsystemd)
pkg_search_module(uuid REQUIRED IMPORTED_TARGET uuid)
pkg_search_module(libevent REQUIRED IMPORTED_TARGET libevent)
pkg_search_module(liburiparser REQUIRED IMPORTED_TARGET liburiparser)
pkg_search_module(libzip REQUIRED IMPORTED_TARGET libzip)

include(CheckCSourceCompiles)

check_c_source_compiles(
  "
  #include <argp.h>
  int main(void) { argp_parse(0, 0, 0, 0, 0, 0); }
  "
  has_argp)

#
# Add components
#

# Common setup for a GGL module
function(ggl_init_module name)
  # TODO: special-case the socket permissions and directory for ggipcd
  cmake_parse_arguments(PARSE_ARGV 1 COMP_ARG "" "SRCDIR;SOCKETDIR;SOCKETMODE"
                        "INCDIRS;LIBS;SOCKETS")
  if("${COMP_ARG_SRCDIR}" STREQUAL "")
    set(COMP_ARG_SRCDIR src)
  endif()
  if("${COMP_ARG_SOCKETDIR}" STREQUAL "")
    set(COMP_ARG_SOCKETDIR /run/greengrass)
  endif()
  if("${COMP_ARG_SOCKETMODE}" STREQUAL "")
    set(COMP_ARG_SOCKETMODE 0660)
  endif()
  if("${COMP_ARG_INCDIRS}" STREQUAL "")
    set(COMP_ARG_INCDIRS include)
  endif()

  file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS "${COMP_ARG_SRCDIR}/*.c"
       "${COMP_ARG_SRCDIR}/*.S")
  list(LENGTH SRCS SRCS_LEN)

  foreach(src ${SRCS})
    set_property(
      SOURCE ${src}
      APPEND_STRING
      PROPERTY COMPILE_FLAGS "-frandom-seed=${src}")
  endforeach()

  # TODO: Allow modules without src to be built, e.g. ggl-constants can delete
  # src/dummy.c
  if(NOT SRCS_LEN EQUAL 0)
    add_library(${name} STATIC ${SRCS})
    target_compile_definitions(${name} PRIVATE "GGL_MODULE=(\"${name}\")")
    target_include_directories(${name} PRIVATE ${COMP_ARG_INCDIRS})
    target_include_directories(${name} SYSTEM INTERFACE ${COMP_ARG_INCDIRS})
    target_link_libraries(${name} PRIVATE ${COMP_ARG_LIBS})
  endif()

  if(GGL_SYSTEMD_SERVICE_BUILD)
    foreach(socket ${COMP_ARG_SOCKETS})
      configure_file(${CMAKE_SOURCE_DIR}/misc/systemd/template.socket.in
                     ${CMAKE_CURRENT_BINARY_DIR}/ggl.${socket}.socket @ONLY)
      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ggl.${socket}.socket
              DESTINATION ${GGL_SYSTEMD_SYSTEM_DIR})
    endforeach()

    file(GLOB_RECURSE SERVICES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_LIST_DIR}/unit/*.service.in")

    foreach(service ${SERVICES})
      get_filename_component(service_filename ${service} NAME_WLE)
      configure_file(${service} ${CMAKE_CURRENT_BINARY_DIR}/${service_filename}
                     @ONLY)
      install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${service_filename}
              DESTINATION ${GGL_SYSTEMD_SYSTEM_DIR})
    endforeach()

  endif()

  file(GLOB BINS CONFIGURE_DEPENDS "bin/*.c")
  foreach(bin ${BINS})
    get_filename_component(bin_name ${bin} NAME_WLE)
    add_executable(${bin_name}-bin ${bin})
    target_compile_definitions(${bin_name}-bin
                               PRIVATE "GGL_MODULE=(\"${name}\")")
    if(NOT SRCS_LEN EQUAL 0)
      target_link_libraries(${bin_name}-bin PRIVATE ${name})
    endif()
    target_link_libraries(${bin_name}-bin PRIVATE ${COMP_ARG_LIBS})
    if(NOT has_argp)
      target_link_libraries(${bin_name}-bin PRIVATE argp)
    endif()
    set_target_properties(${bin_name}-bin PROPERTIES OUTPUT_NAME ${bin_name})
    install(TARGETS ${bin_name}-bin)
  endforeach()
endfunction()

add_subdirectory(ggl-lib)
add_subdirectory(ggl-http)
add_subdirectory(ggl-socket)
add_subdirectory(ggl-file)
add_subdirectory(ggl-process)
add_subdirectory(eventstream)
add_subdirectory(ggl-json)
add_subdirectory(ggl-yaml)
add_subdirectory(ggl-recipe)
add_subdirectory(ggl-cli)
add_subdirectory(ggl-zip)
add_subdirectory(core-bus)
add_subdirectory(core-bus-gg-config)
add_subdirectory(core-bus-aws-iot-mqtt)
add_subdirectory(core-bus-sub-response)
add_subdirectory(aws-iot-call)
add_subdirectory(ggipcd)
add_subdirectory(ggipc-auth)
add_subdirectory(ggipc-client)
add_subdirectory(ggconfigd)
add_subdirectory(gghealthd)
add_subdirectory(deps/backoff_algorithm)
add_subdirectory(ggl-rand)
add_subdirectory(ggl-backoff)
add_subdirectory(deps/core_mqtt)
add_subdirectory(iotcored)
add_subdirectory(ggpubsubd)
add_subdirectory(ggdeploymentd)
add_subdirectory(gg-fleet-statusd)
add_subdirectory(tesd)
add_subdirectory(fleet-provisioning)
add_subdirectory(ggl-config-init)
add_subdirectory(recipe-runner)
add_subdirectory(tes-serverd)
add_subdirectory(ggl-exec)
add_subdirectory(recipe2unit)
add_subdirectory(ggl-semver)
add_subdirectory(ggl-constants)
add_subdirectory(hello-world-component)

if(GGL_SYSTEMD_SERVICE_BUILD)
  add_custom_target(
    greengrass-lite.target ALL
    COMMAND cp "${CMAKE_SOURCE_DIR}/misc/systemd/greengrass-lite.target"
            "${CMAKE_BINARY_DIR}/misc/greengrass-lite.target"
    COMMENT "Nucleus systemd target")
  install(FILES "${CMAKE_BINARY_DIR}/misc/greengrass-lite.target"
          DESTINATION ${GGL_SYSTEMD_SYSTEM_DIR})
  configure_file("${CMAKE_SOURCE_DIR}/misc/systemd/greengrass_dir.conf.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/greengrass_dir.conf" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/greengrass_dir.conf"
          DESTINATION lib/tmpfiles.d)
endif()

if(BUILD_EXAMPLES)
  add_subdirectory(samples/echo-server)
  add_subdirectory(samples/example-client)
  add_subdirectory(samples/mqtt-client)
  add_subdirectory(samples/deployment-client)
  add_subdirectory(samples/status-monitor-client)
  add_subdirectory(configtest)
  add_subdirectory(s3-get-test)
  add_subdirectory(tesd-test)
  add_subdirectory(recipe2unit-test)
  add_subdirectory(ggconfigd-test)
  add_subdirectory(semver-test)
endif()

#
# CPACK deb package generation
#

set(CPACK_GENERATOR DEB)

set(CPACK_DEBIAN_PACKAGE_RELEASE "${LINUX_DISTRO_STRING}")

set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")

set(CPACK_PACKAGE_CONTACT
    "https://github.com/aws-greengrass/aws-greengrass-lite")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)

#
# Uninstall target
#

if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_SOURCE_DIR}/misc/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/misc/cmake_uninstall.cmake" IMMEDIATE @ONLY)

  add_custom_target(
    uninstall
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_BINARY_DIR}/misc/cmake_uninstall.cmake
    COMMENT "Uninstall target")
endif()
