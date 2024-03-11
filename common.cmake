include_guard()

# Utils

include(${CMAKE_CURRENT_LIST_DIR}/utils/fetchContentFromDeps.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/utils/rpath.cmake)

# Defs

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX TRUE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(GCC TRUE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CLANG TRUE)
endif()

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

# Enable a compiler option if supported
macro(try_add_compile_option name option)
  check_cxx_compiler_flag("${option}" compiler_has_${name})
  if(compiler_has_${name})
    add_compile_options("${option}")
  endif()
endmacro()

# Enable a linker option if supported
macro(try_add_link_option name option)
  check_linker_flag(CXX "${option}" linker_has_${name})
  if(linker_has_${name})
    add_link_options("${option}")
  endif()
endmacro()

# Misc

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to not building tests
option(BUILD_TESTING "" OFF)

# Compiler options

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Sets warning flags intended for first-party compilation objects
macro(enable_warnings)
  add_compile_options(-Wall -Wextra -pedantic -Wundef)
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>)
  add_compile_options(-Wno-unused-parameter)
endmacro()

add_compile_options(-fno-common)

# For all built shared-objects, request that exports are explicitly defined
# Note, this isn't necessarily enough - see also version-script option
if(UNIX)
  add_compile_options(-fvisibility=hidden)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(LINUX)
  add_link_options(LINKER:-z,noexecstack)
  add_link_options(LINKER:-z,relro,-z,now)
  try_add_link_option(enable-new-dtags LINKER:--enable-new-dtags)
endif()

# Dead code elimination
try_add_compile_option(function-sections -ffunction-sections)
try_add_compile_option(data-sections -fdata-sections)
try_add_link_option(gc-sections LINKER:--gc-sections)

# LTO
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  cmake_policy(SET CMP0069 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(MSVC)
  if(MSVC_VERSION LESS 1910)
    message(SEND_ERROR "Microsoft Visual Studio 2019 with support for C++17 \
std::variant is required")
  endif()
  if(MSVC_TOOLSET_VERSION LESS 141)
    message(SEND_ERROR "MSVC Toolset v141 is required for std::variant support")
  endif()
  add_compile_options("/EHsc")
endif()

# Debugger options

# Enable an option in profiles with debug info
macro(add_debuginfo_option option)
  add_compile_options($<$<CONFIG:Debug,RelWithDebInfo>:${option}>)
endmacro()

try_add_link_option(compress-debug LINKER:--compress-debug-sections=zlib)

if(LINUX)
  add_debuginfo_option(-ggdb3)
  if(GCC)
    add_debuginfo_option(-Og)
  endif()
endif()

if(APPLE)
  add_debuginfo_option(-g3)
  if(CLANG)
    add_debuginfo_option(-glldb)
  endif()
endif()

# Sanitizers

# Enable a sanitizer
macro(enable_debug_sanitizer name)
  add_compile_options($<$<CONFIG:Debug>:-fsanitize=${name}>)
  add_link_options($<$<CONFIG:Debug>:-fsanitize=${name}>)
endmacro()

if(NOT WIN32)
  if(GCC)
    enable_debug_sanitizer(undefined)
  endif()

  # Thread and address sanitizer are mutually exclusive
  #enable_debug_sanitizer(address)
  #enable_debug_sanitizer(thread)
endif()
