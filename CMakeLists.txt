cmake_minimum_required(VERSION 3.22)
project(greengrass-lite)

# Compiler definitions
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Debugger options
if(NOT APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3 -Og")
endif()
if(NOT APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3")
endif()
if(APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
endif()
if(APPLE AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -glldb")
endif()

# Safety
if(NOT WIN32 AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=null")
endif()

# Uncomment to enable sanitizers - thread & address cannot be combined
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")

if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  # Building for minimized footprint
  # Turn on additional footprint optimizations
  # (CMake already does -Os)

  # Interprocedural optimization
  cmake_policy(SET CMP0069 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

  # Dead code elimination - limited support
  # Note, UNIX+APPLE for, e.g., Clang on Apple
  # UNIX+WIN32 for gcc/glang + mingw on Windows
  if(UNIX
     AND NOT APPLE
     AND NOT WIN32)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options("LINKER:--gc-sections,-Map=link.map")
  endif()
endif()

# For all built shared-objects, request that exports are explicitly defined
# Note, this isn't necessarily enough - see also version-script option
if(UNIX)
  add_compile_options(-fvisibility=hidden)
endif()

# Dependencies
find_package(Threads REQUIRED)

include(./utils/fetchContentFromDeps.cmake)
include(CTest)

fetchContentAddCmake(Catch2 trompeloeil)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
target_compile_features(Catch2 PRIVATE cxx_std_17)

if(UNIX)
  add_compile_options(-fvisibility=hidden)
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

# Targets
add_subdirectory(plugin_api)
add_subdirectory(nucleus)
add_subdirectory(plugins)

add_test(NAME nucleus COMMAND nucleus-tests)
