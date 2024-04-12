include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(gg_pal)

include(../common.cmake)

add_library(gg_pal_interface INTERFACE include/gg_pal/process.hpp)

target_include_directories(gg_pal_interface INTERFACE include)

target_link_libraries(gg_pal_interface INTERFACE gg_plugin_api_min)

if(UNIX)
  set(GGLITE_DETECTED_PLATFORM "posix")
else()
  message(WARNING "Unsupported platform.")
  set(GGLITE_DETECTED_PLATFORM "stub")
endif()

set(GGLITE_PLATFORM
    ${GGLITE_DETECTED_PLATFORM}
    CACHE STRING "GGLite platform")

add_subdirectory(platform/${GGLITE_PLATFORM})
