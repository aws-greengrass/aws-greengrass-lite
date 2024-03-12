cmake_minimum_required(VERSION 3.25)
project(example_plugin1)

include(../../common.cmake)

if(PROJECT_IS_TOP_LEVEL AND NOT LINUX)
  message(
    SEND_ERROR "Building plugins individually is not supported on Mac/Windows.")
endif()

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

enable_warnings()

add_library(example_plugin1 MODULE src/main.cpp)

target_link_libraries(example_plugin1 PRIVATE gg_plugin_api)

set_version_script(example_plugin1 "${CMAKE_CURRENT_SOURCE_DIR}/version.script")

install(TARGETS example_plugin1 DESTINATION plugins)
