include_guard(GLOBAL)
cmake_minimum_required(VERSION 3.25)
project(gg_pal)

include(../common.cmake)

add_library(gg_pal_interface INTERFACE)

target_include_directories(gg_pal_interface INTERFACE include)

if(LINUX)
  set(GGLITE_DETECTED_PLATFORM "linux")
else()
  set(GGLITE_DETECTED_PLATFORM "stub")
endif()

set(GGLITE_PLATFORM
    ${GGLITE_DETECTED_PLATFORM}
    CACHE STRING "GGLite platform")

add_subdirectory(platform/${GGLITE_PLATFORM})
