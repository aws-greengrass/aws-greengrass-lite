cmake_minimum_required(VERSION 3.22)
project(example_layered_plugin)

add_subdirectory(../../plugin_api plugin_api EXCLUDE_FROM_ALL)

add_library(example_layered_plugin MODULE src/main.cpp)

target_link_libraries(example_layered_plugin PRIVATE gg_plugin_api)

if(LINUX)
  target_link_options(
    example_layered_plugin PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

# Uncomment this to install this sample plugin
#install(TARGETS example_layered_plugin DESTINATION plugins)
