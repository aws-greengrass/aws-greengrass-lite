include_guard()

# Utils

include(${CMAKE_CURRENT_LIST_DIR}/utils/fetchContentFromDeps.cmake)

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

# Misc

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to not building tests
option(BUILD_TESTING "" OFF)

# Compiler options

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# For all built shared-objects, request that exports are explicitly defined
# Note, this isn't necessarily enough - see also version-script option
if(UNIX)
  add_compile_options(-fvisibility=hidden)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  # Building for minimized footprint
  # Turn on additional footprint optimizations
  # (CMake already does -Os)

  # Interprocedural optimization
  cmake_policy(SET CMP0069 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

  # Dead code elimination - limited support
  if(LINUX)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options("LINKER:--gc-sections,-Map=link.map")
  endif()
endif()

if(MSVC)
  if(MSVC_VERSION LESS 1910)
    message(SEND_ERROR "Microsoft Visual Studio 2019 with support for C++17 \
std::variant is required")
  endif()
  if(MSVC_TOOLSET_VERSION LESS 141)
    message(SEND_ERROR "MSVC Toolset v141 is required for std::variant support")
  endif()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

# Debugger options

if(LINUX)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3")
  if(GCC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
  endif()
endif()

if(APPLE)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
  if(CLANG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -glldb")
  endif()
endif()

# Sanitizers

if(NOT WIN32)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=null")
  endif()

  #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined")
  # Thread and address sanitizer are mutually exclusive
  #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
  #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
endif()
