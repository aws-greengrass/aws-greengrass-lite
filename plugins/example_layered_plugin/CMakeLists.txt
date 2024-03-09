cmake_minimum_required(VERSION 3.25)
project(example_layered_plugin)

include(../../common.cmake)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

enable_warnings()

add_library(example_layered_plugin MODULE src/main.cpp)

target_link_libraries(example_layered_plugin PRIVATE gg_plugin_api)

if(LINUX)
  target_link_options(
    example_layered_plugin PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

install(TARGETS example_layered_plugin DESTINATION plugins)
