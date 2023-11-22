cmake_minimum_required(VERSION 3.25)
project(greengrass-lite)

# Compiler definitions
set(CMAKE_CXX_STANDARD 17)
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
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

if(UNIX)
  add_compile_options(-fvisibility=hidden)
endif()
if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

# Targets
add_subdirectory(plugin_api)
add_subdirectory(nucleus)
add_subdirectory(plugins)
