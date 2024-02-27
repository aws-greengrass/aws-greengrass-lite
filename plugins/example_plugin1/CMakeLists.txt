cmake_minimum_required(VERSION 3.22)
project(example_plugin1)

add_library(example_plugin1 MODULE src/main.cpp)
target_link_libraries(example_plugin1 PRIVATE gg_plugin_api)

if(UNIX AND NOT APPLE)
  target_link_options(
    example_plugin1 PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.script")
endif()

# Uncomment this to install this sample plugin
#install(TARGETS example_plugin1 DESTINATION plugins)
