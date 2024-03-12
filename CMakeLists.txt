cmake_minimum_required(VERSION 3.25)
project(greengrass-lite)

include(./common.cmake)

if(BUILD_TESTING)
  include(CTest)
endif()

add_subdirectory(nucleus)
add_subdirectory(plugins)
