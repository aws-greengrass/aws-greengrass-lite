cmake_minimum_required(VERSION 3.22)
project(greengrass-lite)

# Compiler definitions
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_CXX_FLAGS_RELEASE "-Os") #-- for size
# set(CMAKE_CXX_FLAGS_RELEASE "-O3") #-- for speed
if(NOT ${MSVC})
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3 -O0")
endif()

# Uncomment to enable sanitizers - thread & address cannot be combined
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=null")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")

# Dependencies
find_package(Threads REQUIRED)

include(FetchContent)
include(./utils/fetchContentFromDeps.cmake)
include(CTest)

fetchContentFromDeps(Catch2)
FetchContent_MakeAvailable(Catch2)
include(Catch)
target_compile_features(Catch2 PRIVATE cxx_std_17)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

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
